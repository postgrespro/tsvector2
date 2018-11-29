/*-------------------------------------------------------------------------
 *
 * tsvector2_rum.c
 *		various text-search functions
 *
 * Portions Copyright (c) 2015-2018, Postgres Professional
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/hash.h"
#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "tsearch/ts_utils.h"
#include "utils/builtins.h"
#if PG_VERSION_NUM >= 120000
#include "utils/float.h"
#endif
#include "utils/typcache.h"

#include "tsvector2.h"
#include "tsvector2_utils.h"

#include <math.h>

/* Use TS_EXEC_PHRASE_AS_AND when TS_EXEC_PHRASE_NO_POS is not defined */
#ifndef TS_EXEC_PHRASE_NO_POS
#define TS_EXEC_PHRASE_NO_POS TS_EXEC_PHRASE_AS_AND
#endif

PG_FUNCTION_INFO_V1(rum_extract_tsvector2);
PG_FUNCTION_INFO_V1(rum_extract_tsvector2_hash);
PG_FUNCTION_INFO_V1(rum_ts_distance_tt);
PG_FUNCTION_INFO_V1(rum_ts_distance_ttf);
PG_FUNCTION_INFO_V1(rum_ts_distance_td);
PG_FUNCTION_INFO_V1(rum_ts_score_tt);
PG_FUNCTION_INFO_V1(rum_ts_score_ttf);
PG_FUNCTION_INFO_V1(rum_ts_score_td);

static Datum build_tsvector2_entry(char *lexeme, int len);
static Datum build_tsvector2_hash_entry(char *lexeme, int len);

typedef Datum (*TSVectorEntryBuilder)(char *lexeme, int len);

static Datum *rum_extract_tsvector2_internal(TSVector2 vector, int32 *nentries,
											Datum **addInfo,
											bool **addInfoIsNull,
											TSVectorEntryBuilder build_tsvector2_entry);

typedef struct
{
	QueryItem  *first_item;
	int		   *map_item_operand;
	bool	   *check;
	bool	   *need_recheck;
	Datum	   *addInfo;
	bool	   *addInfoIsNull;
	bool		recheckPhrase;
}	RumChkVal;

typedef struct
{
	union
	{
		/* Used in rum_ts_distance() */
		struct
		{
			QueryItem **item;
			int16		nitem;
		} item;
		/* Used in rum_tsquery_distance() */
		struct
		{
			QueryItem  *item_first;
			int32		keyn;
		} key;
	} data;
	uint8		wclass;
	int32		pos;
} DocRepresentation;

typedef struct
{
	bool	operandexist;
	WordEntryPos	pos;
}
QueryRepresentationOperand;

typedef struct
{
	TSQuery		query;
	/* Used in rum_tsquery_distance() */
	int		   *map_item_operand;

	QueryRepresentationOperand	   *operandData;
	int			length;
} QueryRepresentation;

typedef struct
{
	int			pos;
	int			p;
	int			q;
	DocRepresentation *begin;
	DocRepresentation *end;
} Extention;

static float weights[] = {1.0/0.1f, 1.0/0.2f, 1.0/0.4f, 1.0/1.0f};

#define QR_GET_OPERAND(q, v)	\
	(&((q)->operandData[ ((QueryItem*)(v)) - GETQUERY((q)->query) ]))

#define SIXTHBIT 0x20
#define LOWERMASK 0x1F

static int
compress_pos(char *target, WordEntryPos *pos, int npos)
{
	int			i;
	uint16		prev = 0,
				delta;
	char	   *ptr;

	ptr = target;
	for (i = 0; i < npos; i++)
	{
		delta = WEP_GETPOS(pos[i]) - WEP_GETPOS(prev);

		while (true)
		{
			if (delta >= SIXTHBIT)
			{
				*ptr = (delta & (~HIGHBIT)) | HIGHBIT;
				ptr++;
				delta >>= 7;
			}
			else
			{
				*ptr = delta | (WEP_GETWEIGHT(pos[i]) << 5);
				ptr++;
				break;
			}
		}
		prev = pos[i];
	}
	return ptr - target;
}

/*
 * sort QueryOperands by (length, word)
 */
static int
compareQueryOperand(const void *a, const void *b, void *arg)
{
	char	   *operand = (char *) arg;
	QueryOperand *qa = (*(QueryOperand *const *) a);
	QueryOperand *qb = (*(QueryOperand *const *) b);

	return tsCompareString(operand + qa->distance, qa->length,
						   operand + qb->distance, qb->length,
						   false);
}

/*
 * Extracting tsvector lexems.
 */

/*
 * Extracts tsvector lexemes from **vector**. Uses **build_tsvector2_entry**
 * callback to extract entry.
 */
static Datum *
rum_extract_tsvector2_internal(TSVector2	vector,
							  int32	   *nentries,
							  Datum   **addInfo,
							  bool	  **addInfoIsNull,
							  TSVectorEntryBuilder build_tsvector2_entry)
{
	Datum	   *entries = NULL;

	*nentries = vector->size;

	AssertPointerAlignment(vector, sizeof(void *));
	if (vector->size > 0)
	{
		uint32		pos;
		int			i;
		WordEntry2  *we = tsvector2_entries(vector);

		entries = (Datum *) palloc(sizeof(Datum) * vector->size);
		*addInfo = (Datum *) palloc(sizeof(Datum) * vector->size);
		*addInfoIsNull = (bool *) palloc(sizeof(bool) * vector->size);

		INITPOS(pos);
		for (i = 0; i < vector->size; i++)
		{
			int				npos = ENTRY_NPOS(vector, we),
							len = ENTRY_LEN(vector, we);
			char		   *lexeme = tsvector2_storage(vector) + pos;

			/* Extract entry using specified method */
			entries[i] = build_tsvector2_entry(lexeme, len);
			if (npos > 0)
			{
				int				posDataSize;
				bytea		   *posData;
				WordEntryPos   *positions = get_lexeme_positions(lexeme, len);

				/*
				 * In some cases compressed positions may take more memory than
				 * uncompressed positions. So allocate memory with a margin.
				 */
				posDataSize = VARHDRSZ + 2 * npos * sizeof(WordEntryPos);
				posData = (bytea *) palloc(posDataSize);

				posDataSize = compress_pos(posData->vl_dat, positions, npos);
				SET_VARSIZE(posData, posDataSize + VARHDRSZ);

				(*addInfo)[i] = PointerGetDatum(posData);
				(*addInfoIsNull)[i] = false;
			}
			else
			{
				(*addInfo)[i] = (Datum) 0;
				(*addInfoIsNull)[i] = true;
			}

			INCRPTR(vector, we, pos);
		}
	}

	return entries;
}

/*
 * Used as callback for rum_extract_tsvector2_internal().
 * Just extract entry from tsvector.
 */
static Datum
build_tsvector2_entry(char *lexeme, int len)
{
	text   *txt;

	txt = cstring_to_text_with_len(lexeme, len);
	return PointerGetDatum(txt);
}

/*
 * Used as callback for rum_extract_tsvector2_internal.
 * Returns hashed entry from tsvector.
 */
static Datum
build_tsvector2_hash_entry(char *lexeme, int len)
{
	int32	hash_value;

	hash_value = hash_any((const unsigned char *) lexeme, len);
	return Int32GetDatum(hash_value);
}

/*
 * Extracts lexemes from tsvector with additional information.
 */
Datum
rum_extract_tsvector2(PG_FUNCTION_ARGS)
{
	TSVector2	vector = PG_GETARG_TSVECTOR2(0);
	int32	   *nentries = (int32 *) PG_GETARG_POINTER(1);
	Datum	  **addInfo = (Datum **) PG_GETARG_POINTER(3);
	bool	  **addInfoIsNull = (bool **) PG_GETARG_POINTER(4);
	Datum	   *entries = NULL;

	entries = rum_extract_tsvector2_internal(vector, nentries, addInfo,
											addInfoIsNull,
											build_tsvector2_entry);
	PG_FREE_IF_COPY(vector, 0);
	PG_RETURN_POINTER(entries);
}

/*
 * Extracts hashed lexemes from tsvector with additional information.
 */
Datum
rum_extract_tsvector2_hash(PG_FUNCTION_ARGS)
{
	TSVector2	vector = PG_GETARG_TSVECTOR2(0);
	int32	   *nentries = (int32 *) PG_GETARG_POINTER(1);
	Datum	  **addInfo = (Datum **) PG_GETARG_POINTER(3);
	bool	  **addInfoIsNull = (bool **) PG_GETARG_POINTER(4);
	Datum	   *entries = NULL;

	entries = rum_extract_tsvector2_internal(vector, nentries, addInfo,
											addInfoIsNull,
											build_tsvector2_hash_entry);

	PG_FREE_IF_COPY(vector, 0);
	PG_RETURN_POINTER(entries);
}

/*
 * Functions used for ranking.
 */

static int
compareDocR(const void *va, const void *vb)
{
	DocRepresentation *a = (DocRepresentation *) va;
	DocRepresentation *b = (DocRepresentation *) vb;

	if (a->pos == b->pos)
		return 0;
	return (a->pos > b->pos) ? 1 : -1;
}

static bool
checkcondition_QueryOperand(void *checkval, QueryOperand *val,
							ExecPhraseData *data)
{
	QueryRepresentation *qr = (QueryRepresentation *) checkval;
	QueryRepresentationOperand	*qro;

	/* Check for rum_tsquery_distance() */
	if (qr->map_item_operand != NULL)
		qro = qr->operandData +
			qr->map_item_operand[(QueryItem *) val - GETQUERY(qr->query)];
	else
		qro = QR_GET_OPERAND(qr, val);

	if (data && qro->operandexist)
	{
		data->npos = 1;
		data->pos = &qro->pos;
		data->allocated = false;
	}

	return qro->operandexist;
}

static bool
Cover(DocRepresentation *doc, uint32 len, QueryRepresentation *qr,
	  Extention *ext)
{
	DocRepresentation *ptr;
	int			lastpos;
	int			i;
	bool		found;

restart:
	lastpos = ext->pos;
	found = false;

	memset(qr->operandData, 0, sizeof(qr->operandData[0]) * qr->length);

	ext->p = 0x7fffffff;
	ext->q = 0;
	ptr = doc + ext->pos;

	/* find upper bound of cover from current position, move up */
	while (ptr - doc < len)
	{
		QueryRepresentationOperand *qro;

		if (qr->map_item_operand != NULL)
		{
			qro = qr->operandData + ptr->data.key.keyn;
			qro->operandexist = true;
			WEP_SETPOS(qro->pos, ptr->pos);
			WEP_SETWEIGHT(qro->pos, ptr->wclass);
		}
		else
		{
			for (i = 0; i < ptr->data.item.nitem; i++)
			{
				qro = QR_GET_OPERAND(qr, ptr->data.item.item[i]);
				qro->operandexist = true;
				WEP_SETPOS(qro->pos, ptr->pos);
				WEP_SETWEIGHT(qro->pos, ptr->wclass);
			}
		}


		if (TS_execute(GETQUERY(qr->query), (void *) qr, TS_EXEC_EMPTY,
					   checkcondition_QueryOperand))
		{
			if (ptr->pos > ext->q)
			{
				ext->q = ptr->pos;
				ext->end = ptr;
				lastpos = ptr - doc;
				found = true;
			}
			break;
		}
		ptr++;
	}

	if (!found)
		return false;

	memset(qr->operandData, 0, sizeof(qr->operandData[0]) * qr->length);

	ptr = doc + lastpos;

	/* find lower bound of cover from found upper bound, move down */
	while (ptr >= doc + ext->pos)
	{
		if (qr->map_item_operand != NULL)
		{
			qr->operandData[ptr->data.key.keyn].operandexist = true;
		}
		else
		{
			for (i = 0; i < ptr->data.item.nitem; i++)
			{
				QueryRepresentationOperand *qro =
									QR_GET_OPERAND(qr, ptr->data.item.item[i]);

				qro->operandexist = true;
				WEP_SETPOS(qro->pos, ptr->pos);
				WEP_SETWEIGHT(qro->pos, ptr->wclass);
			}
		}
		if (TS_execute(GETQUERY(qr->query), (void *) qr, TS_EXEC_CALC_NOT,
					   checkcondition_QueryOperand))
		{
			if (ptr->pos < ext->p)
			{
				ext->begin = ptr;
				ext->p = ptr->pos;
			}
			break;
		}
		ptr--;
	}

	if (ext->p <= ext->q)
	{
		/*
		 * set position for next try to next lexeme after begining of founded
		 * cover
		 */
		ext->pos = (ptr - doc) + 1;
		return true;
	}

	ext->pos++;
	goto restart;
}

static DocRepresentation *
get_docrep_rum(TSVector2 txt, QueryRepresentation *qr, uint32 *doclen)
{
	QueryItem  *item = GETQUERY(qr->query);
	WordEntryPos *post;
	int32		dimt,
				j,
				i,
				nitem;
	int			len = qr->query->size * 4,
				cur = 0;
	DocRepresentation *doc;
	char	   *operand;
	WordEntryPos posnull[1] = {0};

	doc = (DocRepresentation *) palloc(sizeof(DocRepresentation) * len);
	operand = GETOPERAND(qr->query);

	for (i = 0; i < qr->query->size; i++)
	{
		QueryOperand *curoperand;
		int			idx,
					firstidx;

		if (item[i].type != QI_VAL)
			continue;

		curoperand = &item[i].qoperand;

		if (QR_GET_OPERAND(qr, &item[i])->operandexist)
			continue;

		firstidx = idx = find_wordentry(txt, qr->query, curoperand, &nitem);
		if (idx == -1)
			continue;

		while (idx - firstidx < nitem)
		{
			WordEntry2  *entry;
			char	    *lexeme = tsvector2_getlexeme(txt, idx, &entry);

			Assert(!entry->hasoff);
			if (entry->npos)
			{
				dimt = entry->npos;
				post = get_lexeme_positions(lexeme, entry->len);
			}
			else
			{
				dimt = 1;
				post = posnull;
			}

			while (cur + dimt >= len)
			{
				len *= 2;
				doc = (DocRepresentation *) repalloc(doc, sizeof(DocRepresentation) * len);
			}

			for (j = 0; j < dimt; j++)
			{
				if (j == 0)
				{
					int			k;

					doc[cur].data.item.nitem = 0;
					doc[cur].data.item.item = (QueryItem **) palloc(
								sizeof(QueryItem *) * qr->query->size);

					for (k = 0; k < qr->query->size; k++)
					{
						QueryOperand *kptr = &item[k].qoperand;
						QueryOperand *iptr = &item[i].qoperand;

						if (k == i ||
							(item[k].type == QI_VAL &&
							 compareQueryOperand(&kptr, &iptr, operand) == 0))
						{
							QueryRepresentationOperand *qro;

							/*
							 * if k == i, we've already checked above that
							 * it's type == Q_VAL
							 */
							doc[cur].data.item.item[doc[cur].data.item.nitem] =
									item + k;
							doc[cur].data.item.nitem++;

							qro = QR_GET_OPERAND(qr, item + k);

							qro->operandexist = true;
							qro->pos = post[j];

						}
					}
				}
				else
				{
					doc[cur].data.item.nitem = doc[cur - 1].data.item.nitem;
					doc[cur].data.item.item = doc[cur - 1].data.item.item;
				}
				doc[cur].pos = WEP_GETPOS(post[j]);
				doc[cur].wclass = WEP_GETWEIGHT(post[j]);
				cur++;
			}

			idx++;
		}
	}

	*doclen = cur;

	if (cur > 0)
	{
		qsort((void *) doc, cur, sizeof(DocRepresentation), compareDocR);
		return doc;
	}

	pfree(doc);
	return NULL;
}

static double
calc_score_docr(float4 *arrdata, DocRepresentation *doc, uint32 doclen,
				QueryRepresentation *qr, int method)
{
	int32		i;
	Extention	ext;
	double		Wdoc = 0.0;
	double		SumDist = 0.0,
				PrevExtPos = 0.0,
				CurExtPos = 0.0;
	int			NExtent = 0;

	/* Added by SK */
	int		   *cover_keys = (int *)palloc(0);
	int		   *cover_lengths = (int *)palloc(0);
	double	   *cover_ranks = (double *)palloc(0);
	int			ncovers = 0;

	MemSet(&ext, 0, sizeof(Extention));
	while (Cover(doc, doclen, qr, &ext))
	{
		double		Cpos = 0.0;
		double		InvSum = 0.0;
		int			nNoise;
		DocRepresentation *ptr = ext.begin;
		/* Added by SK */
		int			new_cover_idx = 0;
		int			new_cover_key = 0;
		int			nitems = 0;

		while (ptr <= ext.end)
		{
			InvSum += arrdata[ptr->wclass];
			/* SK: Quick and dirty hash key. Hope collisions will be not too frequent. */
			new_cover_key = new_cover_key << 1;
			/* For rum_ts_distance() */
			if (qr->map_item_operand == NULL)
				new_cover_key += (int)(uintptr_t)ptr->data.item.item;
			/* For rum_tsquery_distance() */
			else
				new_cover_key += (int)(uintptr_t)ptr->data.key.item_first;
			ptr++;
		}

		/* Added by SK */
		/* TODO: use binary tree?.. */
		while(new_cover_idx < ncovers)
		{
			if(new_cover_key == cover_keys[new_cover_idx])
				break;
			new_cover_idx ++;
		}

		if(new_cover_idx == ncovers)
		{
			cover_keys = (int *) repalloc(cover_keys, sizeof(int) *
										  (ncovers + 1));
			cover_lengths = (int *) repalloc(cover_lengths, sizeof(int) *
											 (ncovers + 1));
			cover_ranks = (double *) repalloc(cover_ranks, sizeof(double) *
											  (ncovers + 1));

			cover_lengths[ncovers] = 0;
			cover_ranks[ncovers] = 0;

			ncovers ++;
		}

		cover_keys[new_cover_idx] = new_cover_key;

		/* Compute the number of query terms in the cover */
		for (i = 0; i < qr->length; i++)
			if (qr->operandData[i].operandexist)
				nitems++;

		Cpos = ((double) (ext.end - ext.begin + 1)) / InvSum;

		if (nitems > 0)
			Cpos *= nitems;

		/*
		 * if doc are big enough then ext.q may be equal to ext.p due to limit
		 * of posional information. In this case we approximate number of
		 * noise word as half cover's length
		 */
		nNoise = (ext.q - ext.p) - (ext.end - ext.begin);
		if (nNoise < 0)
			nNoise = (ext.end - ext.begin) / 2;
		/* SK: Wdoc += Cpos / ((double) (1 + nNoise)); */
		cover_lengths[new_cover_idx] ++;
		cover_ranks[new_cover_idx] += Cpos / ((double) (1 + nNoise))
			/ cover_lengths[new_cover_idx] / cover_lengths[new_cover_idx]
				/ 1.64493406685;

		CurExtPos = ((double) (ext.q + ext.p)) / 2.0;
		if (NExtent > 0 && CurExtPos > PrevExtPos		/* prevent devision by
														 * zero in a case of
				multiple lexize */ )
			SumDist += 1.0 / (CurExtPos - PrevExtPos);

		PrevExtPos = CurExtPos;
		NExtent++;
	}

	/* Added by SK */
	for(i = 0; i < ncovers; i++)
		Wdoc += cover_ranks[i];

	if ((method & RANK_NORM_EXTDIST) && NExtent > 0 && SumDist > 0)
		Wdoc /= ((double) NExtent) / SumDist;

	if (method & RANK_NORM_RDIVRPLUS1)
		Wdoc /= (Wdoc + 1);

	pfree(cover_keys);
	pfree(cover_lengths);
	pfree(cover_ranks);

	return (float4) Wdoc;
}

static float4
calc_score(float4 *arrdata, TSVector2 txt, TSQuery query, int method)
{
	DocRepresentation *doc;
	uint32		len,
				doclen = 0;
	double		Wdoc = 0.0;
	QueryRepresentation qr;

	qr.query = query;
	qr.map_item_operand = NULL;
	qr.operandData = palloc0(sizeof(qr.operandData[0]) * query->size);
	qr.length = query->size;

	doc = get_docrep_rum(txt, &qr, &doclen);
	if (!doc)
	{
		pfree(qr.operandData);
		return 0.0;
	}

	Wdoc = calc_score_docr(arrdata, doc, doclen, &qr, method);

	if ((method & RANK_NORM_LOGLENGTH) && txt->size > 0)
		Wdoc /= log((double) (count_length(txt) + 1));

	if (method & RANK_NORM_LENGTH)
	{
		len = count_length(txt);
		if (len > 0)
			Wdoc /= (double) len;
	}

	if ((method & RANK_NORM_UNIQ) && txt->size > 0)
		Wdoc /= (double) (txt->size);

	if ((method & RANK_NORM_LOGUNIQ) && txt->size > 0)
		Wdoc /= log((double) (txt->size + 1)) / log(2.0);

	pfree(doc);
	pfree(qr.operandData);

	return (float4) Wdoc;
}

/*
 * Implementation of <=> operator. Uses default normalization method.
 */
Datum
rum_ts_distance_tt(PG_FUNCTION_ARGS)
{
	TSVector2	txt = PG_GETARG_TSVECTOR2(0);
	TSQuery		query = PG_GETARG_TSQUERY(1);
	float4		res;

	res = calc_score(weights, txt, query, DEF_NORM_METHOD);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(query, 1);
	if (res == 0)
		PG_RETURN_FLOAT4(get_float4_infinity());
	else
		PG_RETURN_FLOAT4(1.0 / res);
}

/*
 * Implementation of <=> operator. Uses specified normalization method.
 */
Datum
rum_ts_distance_ttf(PG_FUNCTION_ARGS)
{
	TSVector2	txt = PG_GETARG_TSVECTOR2(0);
	TSQuery		query = PG_GETARG_TSQUERY(1);
	int			method = PG_GETARG_INT32(2);
	float4		res;

	res = calc_score(weights, txt, query, method);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(query, 1);
	if (res == 0)
		PG_RETURN_FLOAT4(get_float4_infinity());
	else
		PG_RETURN_FLOAT4(1.0 / res);
}

static float4
calc_score_parse_opt(TSVector2 txt, HeapTupleHeader d)
{
	Oid			tupType = HeapTupleHeaderGetTypeId(d);
	int32		tupTypmod = HeapTupleHeaderGetTypMod(d);
	TupleDesc	tupdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);
	HeapTupleData tuple;

	TSQuery		query;
	int			method;
	bool		isnull;
	float4		res;

	tuple.t_len = HeapTupleHeaderGetDatumLength(d);
	ItemPointerSetInvalid(&(tuple.t_self));
	tuple.t_tableOid = InvalidOid;
	tuple.t_data = d;

	query = DatumGetTSQuery(fastgetattr(&tuple, 1, tupdesc, &isnull));
	if (isnull)
	{
		ReleaseTupleDesc(tupdesc);
		elog(ERROR, "NULL query value is not allowed");
	}

	method = DatumGetInt32(fastgetattr(&tuple, 2, tupdesc, &isnull));
	if (isnull)
		method = 0;

	res = calc_score(weights, txt, query, method);

	ReleaseTupleDesc(tupdesc);

	return res;
}

/*
 * Implementation of <=> operator. Uses specified normalization method.
 */
Datum
rum_ts_distance_td(PG_FUNCTION_ARGS)
{
	TSVector2	txt = PG_GETARG_TSVECTOR2(0);
	HeapTupleHeader d = PG_GETARG_HEAPTUPLEHEADER(1);
	float4		res;

	res = calc_score_parse_opt(txt, d);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(d, 1);

	if (res == 0)
		PG_RETURN_FLOAT4(get_float4_infinity());
	else
		PG_RETURN_FLOAT4(1.0 / res);
}

/*
 * Calculate score (inverted distance). Uses default normalization method.
 */
Datum
rum_ts_score_tt(PG_FUNCTION_ARGS)
{
	TSVector2	txt = PG_GETARG_TSVECTOR2(0);
	TSQuery		query = PG_GETARG_TSQUERY(1);
	float4		res;

	res = calc_score(weights, txt, query, DEF_NORM_METHOD);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(query, 1);

	PG_RETURN_FLOAT4(res);
}

/*
 * Calculate score (inverted distance). Uses specified normalization method.
 */
Datum
rum_ts_score_ttf(PG_FUNCTION_ARGS)
{
	TSVector2	txt = PG_GETARG_TSVECTOR2(0);
	TSQuery		query = PG_GETARG_TSQUERY(1);
	int			method = PG_GETARG_INT32(2);
	float4		res;

	res = calc_score(weights, txt, query, method);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(query, 1);

	PG_RETURN_FLOAT4(res);
}

/*
 * Calculate score (inverted distance). Uses specified normalization method.
 */
Datum
rum_ts_score_td(PG_FUNCTION_ARGS)
{
	TSVector2	txt = PG_GETARG_TSVECTOR2(0);
	HeapTupleHeader d = PG_GETARG_HEAPTUPLEHEADER(1);
	float4		res;

	res = calc_score_parse_opt(txt, d);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(d, 1);

	PG_RETURN_FLOAT4(res);
}
