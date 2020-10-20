/*-------------------------------------------------------------------------
 *
 * tsvector2_rank.c
 *		rank tsvector2 by tsquery
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 2018, PostgresPro
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <limits.h>
#include <math.h>

#include "tsearch/ts_utils.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "miscadmin.h"

#include "tsvector2.h"
#include "tsvector2_utils.h"

static const float weights[] = {0.1f, 0.2f, 0.4f, 1.0f};

#define wpos(wep)	( w[ WEP_GETWEIGHT(wep) ] )

static float calc_rank_or(const float *w, TSVector2 t, TSQuery q);
static float calc_rank_and(const float *w, TSVector2 t, TSQuery q);

/*
 * Returns a weight of a word collocation
 */
static float4
word_distance(int32 w)
{
	if (w > 100)
		return 1e-30f;

	return 1.0 / (1.005 + 0.05 * exp(((float4) w) / 1.5 - 2));
}

int
count_length(TSVector2 t)
{
	int			i,
				len = 0;

	for (i = 0; i < t->size; i++)
	{
		WordEntry2  *entry = UNWRAP_ENTRY(t, tsvector2_entries(t) + i);

		Assert(!entry->hasoff);
		len += (entry->npos == 0) ? 1 : entry->npos;
	}

	return len;
}


/*
 * Returns a pointer to a WordEntry2's array corresponding to 'item' from
 * tsvector 't'. 'q' is the TSQuery containing 'item'.
 * Returns NULL if not found.
 */
int
find_wordentry(TSVector2 t, TSQuery q, QueryOperand *item, int32 *nitem)
{
#define WordECompareQueryItem(s,l,q,i,m) \
	ts2_compare_string((q) + (i)->distance, (i)->length,	\
					s, l, (m))

	int			StopLow = 0;
	int			StopHigh = t->size;
	int			StopMiddle = StopHigh;
	int			difference;
	char	   *lexeme;
	WordEntry2  *we;

	*nitem = 0;

	/* Loop invariant: StopLow <= item < StopHigh */
	while (StopLow < StopHigh)
	{
		StopMiddle = StopLow + (StopHigh - StopLow) / 2;
		lexeme = tsvector2_getlexeme(t, StopMiddle, &we);

		Assert(!we->hasoff);
		difference = WordECompareQueryItem(lexeme, we->len,
										   GETOPERAND(q), item, false);

		if (difference == 0)
		{
			StopHigh = StopMiddle;
			*nitem = 1;
			break;
		}
		else if (difference > 0)
			StopLow = StopMiddle + 1;
		else
			StopHigh = StopMiddle;
	}

	if (item->prefix)
	{
		if (StopLow >= StopHigh)
			StopMiddle = StopHigh;

		*nitem = 0;

		while (StopMiddle < t->size)
		{
			lexeme = tsvector2_getlexeme(t, StopMiddle, &we);

			Assert(!we->hasoff);
			if (WordECompareQueryItem(lexeme, we->len, GETOPERAND(q), item, true) != 0)
				break;

			(*nitem)++;
			StopMiddle++;
		}
	}

	return (*nitem > 0) ? StopHigh : -1;
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

	return ts2_compare_string(operand + qa->distance, qa->length,
						   operand + qb->distance, qb->length,
						   false);
}

/*
 * Returns a sorted, de-duplicated array of QueryOperands in a query.
 * The returned QueryOperands are pointers to the original QueryOperands
 * in the query.
 *
 * Length of the returned array is stored in *size
 */
static QueryOperand **
SortAndUniqItems(TSQuery q, int *size)
{
	char	   *operand = GETOPERAND(q);
	QueryItem  *item = GETQUERY(q);
	QueryOperand **res,
			  **ptr,
			  **prevptr;

	ptr = res = (QueryOperand **) palloc(sizeof(QueryOperand *) * *size);

	/* Collect all operands from the tree to res */
	while ((*size)--)
	{
		if (item->type == QI_VAL)
		{
			*ptr = (QueryOperand *) item;
			ptr++;
		}
		item++;
	}

	*size = ptr - res;
	if (*size < 2)
		return res;

	qsort_arg(res, *size, sizeof(QueryOperand *), compareQueryOperand, (void *) operand);

	ptr = res + 1;
	prevptr = res;

	/* remove duplicates */
	while (ptr - res < *size)
	{
		if (compareQueryOperand((void *) ptr, (void *) prevptr, (void *) operand) != 0)
		{
			prevptr++;
			*prevptr = *ptr;
		}
		ptr++;
	}

	*size = prevptr + 1 - res;
	return res;
}

static float
calc_rank_and(const float *w, TSVector2 t, TSQuery q)
{
	WordEntryPos **pos;
	uint16	   *npos;
	WordEntryPos posnull[1] = {0};
	int			i,
				k,
				l,
				p;
	WordEntryPos *post,
			   *ct;
	int32		dimt,
				lenct,
				dist,
				nitem;
	float		res = -1.0;
	QueryOperand **item;
	int			size = q->size;

	item = SortAndUniqItems(q, &size);
	if (size < 2)
	{
		pfree(item);
		return calc_rank_or(w, t, q);
	}
	pos = (WordEntryPos **) palloc0(sizeof(WordEntryPos *) * q->size);
	npos = (uint16 *) palloc0(sizeof(uint16) * q->size);

	/* posnull is a dummy WordEntryPos array to use when npos == 0 */
	WEP_SETPOS(posnull[0], MAXENTRYPOS - 1);

	for (i = 0; i < size; i++)
	{
		int			idx = find_wordentry(t, q, item[i], &nitem),
					firstidx;

		if (idx == -1)
			continue;

		firstidx = idx;

		while (idx - firstidx < nitem)
		{
			WordEntry2  *entry;

			char	   *lexeme = tsvector2_getlexeme(t, idx, &entry);

			Assert(!entry->hasoff);
			if (entry->npos)
			{
				pos[i] = get_lexeme_positions(lexeme, entry->len);
				npos[i] = entry->npos;
			}
			else
			{
				pos[i] = posnull;
				npos[i] = 1;
			}

			post = pos[i];
			dimt = npos[i];

			for (k = 0; k < i; k++)
			{
				if (!pos[k])
					continue;
				lenct = npos[k];
				ct = pos[k];
				for (l = 0; l < dimt; l++)
				{
					for (p = 0; p < lenct; p++)
					{
						dist = Abs((int) WEP_GETPOS(post[l]) - (int) WEP_GETPOS(ct[p]));
						if (dist || (dist == 0 && (pos[i] == posnull || pos[k] == posnull)))
						{
							float		curw;

							if (!dist)
								dist = MAXENTRYPOS;
							curw = sqrt(wpos(post[l]) * wpos(ct[p]) * word_distance(dist));
							res = (res < 0) ? curw : 1.0 - (1.0 - res) * (1.0 - curw);
						}
					}
				}
			}

			idx++;
		}
	}
	pfree(pos);
	pfree(npos);
	pfree(item);
	return res;
}

static float
calc_rank_or(const float *w, TSVector2 t, TSQuery q)
{
	/* A dummy WordEntryPos array to use when lexeme hasn't positions */
	WordEntryPos posnull[1] = {0};
	WordEntryPos *post;
	int32		dimt,
				j,
				i,
				nitem;
	float		res = 0.0;
	QueryOperand **item;
	int			size = q->size;

	item = SortAndUniqItems(q, &size);

	for (i = 0; i < size; i++)
	{
		int			idx,
					firstidx;
		float		resj,
					wjm;
		int32		jm;

		idx = find_wordentry(t, q, item[i], &nitem);
		if (idx == -1)
			continue;

		firstidx = idx;

		while (idx - firstidx < nitem)
		{
			WordEntry2  *entry;
			char	   *lexeme = tsvector2_getlexeme(t, idx, &entry);

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

			resj = 0.0;
			wjm = -1.0;
			jm = 0;
			for (j = 0; j < dimt; j++)
			{
				resj = resj + wpos(post[j]) / ((j + 1) * (j + 1));
				if (wpos(post[j]) > wjm)
				{
					wjm = wpos(post[j]);
					jm = j;
				}
			}
			res = res + (wjm + resj - wjm / ((jm + 1) * (jm + 1))) / 1.64493406685;

			idx++;
		}
	}
	if (size > 0)
		res = res / size;
	pfree(item);
	return res;
}

static float
calc_rank(const float *w, TSVector2 t, TSQuery q, int32 method)
{
	QueryItem  *item = GETQUERY(q);
	float		res = 0.0;
	int			len;

	if (!t->size || !q->size)
		return 0.0;

	/* XXX: What about NOT? */
	res = (item->type == QI_OPR && (item->qoperator.oper == OP_AND ||
									item->qoperator.oper == OP_PHRASE)) ?
		calc_rank_and(w, t, q) :
		calc_rank_or(w, t, q);

	if (res < 0)
		res = 1e-20f;

	if ((method & RANK_NORM_LOGLENGTH) && t->size > 0)
		res /= log((double) (count_length(t) + 1)) / log(2.0);

	if (method & RANK_NORM_LENGTH)
	{
		len = count_length(t);
		if (len > 0)
			res /= (float) len;
	}

	/* RANK_NORM_EXTDIST not applicable */

	if ((method & RANK_NORM_UNIQ) && t->size > 0)
		res /= (float) (t->size);

	if ((method & RANK_NORM_LOGUNIQ) && t->size > 0)
		res /= log((double) (t->size + 1)) / log(2.0);

	if (method & RANK_NORM_RDIVRPLUS1)
		res /= (res + 1);

	return res;
}

static const float *
getWeights(ArrayType *win)
{
	static float ws[lengthof(weights)];
	int			i;
	float4	   *arrdata;

	if (win == NULL)
		return weights;

	if (ARR_NDIM(win) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
				 errmsg("array of weight must be one-dimensional")));

	if (ArrayGetNItems(ARR_NDIM(win), ARR_DIMS(win)) < lengthof(weights))
		ereport(ERROR,
				(errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
				 errmsg("array of weight is too short")));

	if (array_contains_nulls(win))
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("array of weight must not contain nulls")));

	arrdata = (float4 *) ARR_DATA_PTR(win);
	for (i = 0; i < lengthof(weights); i++)
	{
		ws[i] = (arrdata[i] >= 0) ? arrdata[i] : weights[i];
		if (ws[i] > 1.0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("weight out of range")));
	}

	return ws;
}

PG_FUNCTION_INFO_V1(tsvector2_rank_wttf);
Datum
tsvector2_rank_wttf(PG_FUNCTION_ARGS)
{
	ArrayType  *win = (ArrayType *) PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
	TSVector2	txt = PG_GETARG_TSVECTOR2(1);
	TSQuery		query = PG_GETARG_TSQUERY(2);
	int			method = PG_GETARG_INT32(3);
	float		res;

	res = calc_rank(getWeights(win), txt, query, method);

	PG_FREE_IF_COPY(win, 0);
	PG_FREE_IF_COPY(txt, 1);
	PG_FREE_IF_COPY(query, 2);
	PG_RETURN_FLOAT4(res);
}

PG_FUNCTION_INFO_V1(tsvector2_rank_wtt);
Datum
tsvector2_rank_wtt(PG_FUNCTION_ARGS)
{
	ArrayType  *win = (ArrayType *) PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
	TSVector2	txt = PG_GETARG_TSVECTOR2(1);
	TSQuery		query = PG_GETARG_TSQUERY(2);
	float		res;

	res = calc_rank(getWeights(win), txt, query, DEF_NORM_METHOD);

	PG_FREE_IF_COPY(win, 0);
	PG_FREE_IF_COPY(txt, 1);
	PG_FREE_IF_COPY(query, 2);
	PG_RETURN_FLOAT4(res);
}

PG_FUNCTION_INFO_V1(tsvector2_rank_ttf);
Datum
tsvector2_rank_ttf(PG_FUNCTION_ARGS)
{
	TSVector2	txt = PG_GETARG_TSVECTOR2(0);
	TSQuery		query = PG_GETARG_TSQUERY(1);
	int			method = PG_GETARG_INT32(2);
	float		res;

	res = calc_rank(getWeights(NULL), txt, query, method);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(query, 1);
	PG_RETURN_FLOAT4(res);
}

PG_FUNCTION_INFO_V1(tsvector2_rank_tt);
Datum
tsvector2_rank_tt(PG_FUNCTION_ARGS)
{
	TSVector2	txt = PG_GETARG_TSVECTOR2(0);
	TSQuery		query = PG_GETARG_TSQUERY(1);
	float		res;

	res = calc_rank(getWeights(NULL), txt, query, DEF_NORM_METHOD);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(query, 1);
	PG_RETURN_FLOAT4(res);
}

typedef struct
{
	union
	{
		struct
		{						/* compiled doc representation */
			QueryItem **items;
			int32		nitem;
		}			query;
		struct
		{						/* struct is used for preparing doc
								 * representation */
			QueryItem  *item;
			int32		idx;
		}			map;
	}			data;
	WordEntryPos pos;
} DocRepresentation;

static int
compareDocR(const void *va, const void *vb)
{
	const DocRepresentation *a = (const DocRepresentation *) va;
	const DocRepresentation *b = (const DocRepresentation *) vb;

	if (WEP_GETPOS(a->pos) == WEP_GETPOS(b->pos))
	{
		if (WEP_GETWEIGHT(a->pos) == WEP_GETWEIGHT(b->pos))
		{
			if (a->data.map.idx == b->data.map.idx)
				return 0;

			return (a->data.map.idx > b->data.map.idx) ? 1 : -1;
		}

		return (WEP_GETWEIGHT(a->pos) > WEP_GETWEIGHT(b->pos)) ? 1 : -1;
	}

	return (WEP_GETPOS(a->pos) > WEP_GETPOS(b->pos)) ? 1 : -1;
}

#define MAXQROPOS	MAXENTRYPOS
typedef struct
{
	bool		operandexists;
	bool		reverseinsert;	/* indicates insert order, true means
								 * descending order */
	uint32		npos;
	WordEntryPos pos[MAXQROPOS];
} QueryRepresentationOperand;

typedef struct
{
	TSQuery		query;
	QueryRepresentationOperand *operandData;
} QueryRepresentation;

#define QR_GET_OPERAND_DATA(q, v) \
	( (q)->operandData + (((QueryItem*)(v)) - GETQUERY((q)->query)) )

static TSTernaryValue
checkcondition_QueryOperand(void *checkval, QueryOperand *val, ExecPhraseData *data)
{
	QueryRepresentation *qr = (QueryRepresentation *) checkval;
	QueryRepresentationOperand *opData = QR_GET_OPERAND_DATA(qr, val);

	if (!opData->operandexists)
		return TS_NO;

	if (data)
	{
		data->npos = opData->npos;
		data->pos = opData->pos;
		if (opData->reverseinsert)
			data->pos += MAXQROPOS - opData->npos;
	}

	return TS_YES;
}

typedef struct
{
	int			pos;
	int			p;
	int			q;
	DocRepresentation *begin;
	DocRepresentation *end;
} CoverExt;

static void
resetQueryRepresentation(QueryRepresentation *qr, bool reverseinsert)
{
	int			i;

	for (i = 0; i < qr->query->size; i++)
	{
		qr->operandData[i].operandexists = false;
		qr->operandData[i].reverseinsert = reverseinsert;
		qr->operandData[i].npos = 0;
	}
}

static void
fillQueryRepresentationData(QueryRepresentation *qr, DocRepresentation *entry)
{
	int			i;
	int			lastPos;
	QueryRepresentationOperand *opData;

	for (i = 0; i < entry->data.query.nitem; i++)
	{
		if (entry->data.query.items[i]->type != QI_VAL)
			continue;

		opData = QR_GET_OPERAND_DATA(qr, entry->data.query.items[i]);

		opData->operandexists = true;

		if (opData->npos == 0)
		{
			lastPos = (opData->reverseinsert) ? (MAXQROPOS - 1) : 0;
			opData->pos[lastPos] = entry->pos;
			opData->npos++;
			continue;
		}

		lastPos = opData->reverseinsert ?
			(MAXQROPOS - opData->npos) :
			(opData->npos - 1);

		if (WEP_GETPOS(opData->pos[lastPos]) != WEP_GETPOS(entry->pos))
		{
			lastPos = opData->reverseinsert ?
				(MAXQROPOS - 1 - opData->npos) :
				(opData->npos);

			opData->pos[lastPos] = entry->pos;
			opData->npos++;
		}
	}
}

static bool
Cover(DocRepresentation *doc, int len, QueryRepresentation *qr, CoverExt *ext)
{
	DocRepresentation *ptr;
	int			lastpos = ext->pos;
	bool		found = false;

	/*
	 * since this function recurses, it could be driven to stack overflow.
	 * (though any decent compiler will optimize away the tail-recursion.
	 */
	check_stack_depth();

	resetQueryRepresentation(qr, false);

	ext->p = INT_MAX;
	ext->q = 0;
	ptr = doc + ext->pos;

	/* find upper bound of cover from current position, move up */
	while (ptr - doc < len)
	{
		fillQueryRepresentationData(qr, ptr);

		if (TS_execute(GETQUERY(qr->query), (void *) qr,
					   TS_EXEC_EMPTY, checkcondition_QueryOperand))
		{
			if (WEP_GETPOS(ptr->pos) > ext->q)
			{
				ext->q = WEP_GETPOS(ptr->pos);
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

	resetQueryRepresentation(qr, true);

	ptr = doc + lastpos;

	/* find lower bound of cover from found upper bound, move down */
	while (ptr >= doc + ext->pos)
	{
		/*
		 * we scan doc from right to left, so pos info in reverse order!
		 */
		fillQueryRepresentationData(qr, ptr);

		if (TS_execute(GETQUERY(qr->query), (void *) qr,
					   TS_EXEC_CALC_NOT, checkcondition_QueryOperand))
		{
			if (WEP_GETPOS(ptr->pos) < ext->p)
			{
				ext->begin = ptr;
				ext->p = WEP_GETPOS(ptr->pos);
			}
			break;
		}
		ptr--;
	}

	if (ext->p <= ext->q)
	{
		/*
		 * set position for next try to next lexeme after beginning of found
		 * cover
		 */
		ext->pos = (ptr - doc) + 1;
		return true;
	}

	ext->pos++;
	return Cover(doc, len, qr, ext);
}

static DocRepresentation *
get_docrep(TSVector2 txt, QueryRepresentation *qr, int *doclen)
{
	QueryItem  *item = GETQUERY(qr->query);
	int32		dimt,			/* number of 'post' items */
				j,
				i,
				nitem;
	int			len = qr->query->size * 4,
				cur = 0;
	DocRepresentation *doc;

	doc = (DocRepresentation *) palloc(sizeof(DocRepresentation) * len);

	/*
	 * Iterate through query to make DocRepresentaion for words and it's
	 * entries satisfied by query
	 */
	for (i = 0; i < qr->query->size; i++)
	{
		int			idx,
					firstidx;
		QueryOperand *curoperand;
		WordEntryPos *post;

		if (item[i].type != QI_VAL)
			continue;

		curoperand = &item[i].qoperand;

		idx = find_wordentry(txt, qr->query, curoperand, &nitem);
		if (idx < 0)
			continue;

		firstidx = idx;

		/* iterations over entries in tsvector */
		while (idx - firstidx < nitem)
		{
			WordEntry2  *entry;
			char	   *lex = tsvector2_getlexeme(txt, idx, &entry);

			Assert(!entry->hasoff);
			if (entry->npos)
			{
				dimt = entry->npos;
				post = get_lexeme_positions(lex, entry->len);
			}
			else
			{
				/* ignore words without positions */
				idx++;
				continue;
			}

			while (cur + dimt >= len)
			{
				len *= 2;
				doc = (DocRepresentation *) repalloc(doc, sizeof(DocRepresentation) * len);
			}

			/* iterations over entry's positions */
			for (j = 0; j < dimt; j++)
			{
				if (curoperand->weight == 0 ||
					curoperand->weight & (1 << WEP_GETWEIGHT(post[j])))
				{
					doc[cur].pos = post[j];
					doc[cur].data.map.idx = idx;
					doc[cur].data.map.item = (QueryItem *) curoperand;
					cur++;
				}
			}
			idx++;
		}
	}

	if (cur > 0)
	{
		DocRepresentation *rptr = doc + 1,
				   *wptr = doc,
					storage;

		/*
		 * Sort representation in ascending order by pos and entry
		 */
		qsort((void *) doc, cur, sizeof(DocRepresentation), compareDocR);

		/*
		 * Join QueryItem per WordEntry2 and it's position
		 */
		storage.pos = doc->pos;
		storage.data.query.items = palloc(sizeof(QueryItem *) * qr->query->size);
		storage.data.query.items[0] = doc->data.map.item;
		storage.data.query.nitem = 1;

		while (rptr - doc < cur)
		{
			if (rptr->pos == (rptr - 1)->pos &&
				rptr->data.map.idx == (rptr - 1)->data.map.idx)
			{
				storage.data.query.items[storage.data.query.nitem] = rptr->data.map.item;
				storage.data.query.nitem++;
			}
			else
			{
				*wptr = storage;
				wptr++;
				storage.pos = rptr->pos;
				storage.data.query.items = palloc(sizeof(QueryItem *) * qr->query->size);
				storage.data.query.items[0] = rptr->data.map.item;
				storage.data.query.nitem = 1;
			}

			rptr++;
		}

		*wptr = storage;
		wptr++;

		*doclen = wptr - doc;
		return doc;
	}

	pfree(doc);
	return NULL;
}

static float4
calc_rank_cd(const float4 *arrdata, TSVector2 txt, TSQuery query, int method)
{
	DocRepresentation *doc;
	int			len,
				i,
				doclen = 0;
	CoverExt	ext;
	double		Wdoc = 0.0;
	double		invws[lengthof(weights)];
	double		SumDist = 0.0,
				PrevExtPos = 0.0,
				CurExtPos = 0.0;
	int			NExtent = 0;
	QueryRepresentation qr;


	for (i = 0; i < lengthof(weights); i++)
	{
		invws[i] = ((double) ((arrdata[i] >= 0) ? arrdata[i] : weights[i]));
		if (invws[i] > 1.0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("weight out of range")));
		invws[i] = 1.0 / invws[i];
	}

	qr.query = query;
	qr.operandData = (QueryRepresentationOperand *)
		palloc0(sizeof(QueryRepresentationOperand) * query->size);

	doc = get_docrep(txt, &qr, &doclen);
	if (!doc)
	{
		pfree(qr.operandData);
		return 0.0;
	}

	MemSet(&ext, 0, sizeof(CoverExt));
	while (Cover(doc, doclen, &qr, &ext))
	{
		double		Cpos = 0.0;
		double		InvSum = 0.0;
		int			nNoise;
		DocRepresentation *ptr = ext.begin;

		while (ptr <= ext.end)
		{
			InvSum += invws[WEP_GETWEIGHT(ptr->pos)];
			ptr++;
		}

		Cpos = ((double) (ext.end - ext.begin + 1)) / InvSum;

		/*
		 * if doc are big enough then ext.q may be equal to ext.p due to limit
		 * of positional information. In this case we approximate number of
		 * noise word as half cover's length
		 */
		nNoise = (ext.q - ext.p) - (ext.end - ext.begin);
		if (nNoise < 0)
			nNoise = (ext.end - ext.begin) / 2;
		Wdoc += Cpos / ((double) (1 + nNoise));

		CurExtPos = ((double) (ext.q + ext.p)) / 2.0;
		if (NExtent > 0 && CurExtPos > PrevExtPos	/* prevent division by
													 * zero in a case of
			  * multiple lexize */ )
			SumDist += 1.0 / (CurExtPos - PrevExtPos);

		PrevExtPos = CurExtPos;
		NExtent++;
	}

	if ((method & RANK_NORM_LOGLENGTH) && txt->size > 0)
		Wdoc /= log((double) (count_length(txt) + 1));

	if (method & RANK_NORM_LENGTH)
	{
		len = count_length(txt);
		if (len > 0)
			Wdoc /= (double) len;
	}

	if ((method & RANK_NORM_EXTDIST) && NExtent > 0 && SumDist > 0)
		Wdoc /= ((double) NExtent) / SumDist;

	if ((method & RANK_NORM_UNIQ) && txt->size > 0)
		Wdoc /= (double) (txt->size);

	if ((method & RANK_NORM_LOGUNIQ) && txt->size > 0)
		Wdoc /= log((double) (txt->size + 1)) / log(2.0);

	if (method & RANK_NORM_RDIVRPLUS1)
		Wdoc /= (Wdoc + 1);

	pfree(doc);

	pfree(qr.operandData);

	return (float4) Wdoc;
}

PG_FUNCTION_INFO_V1(tsvector2_rankcd_wttf);
Datum
tsvector2_rankcd_wttf(PG_FUNCTION_ARGS)
{
	ArrayType  *win = (ArrayType *) PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
	TSVector2	txt = PG_GETARG_TSVECTOR2(1);
	TSQuery		query = PG_GETARG_TSQUERY(2);
	int			method = PG_GETARG_INT32(3);
	float		res;

	res = calc_rank_cd(getWeights(win), txt, query, method);

	PG_FREE_IF_COPY(win, 0);
	PG_FREE_IF_COPY(txt, 1);
	PG_FREE_IF_COPY(query, 2);
	PG_RETURN_FLOAT4(res);
}

PG_FUNCTION_INFO_V1(tsvector2_rankcd_wtt);
Datum
tsvector2_rankcd_wtt(PG_FUNCTION_ARGS)
{
	ArrayType  *win = (ArrayType *) PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
	TSVector2	txt = PG_GETARG_TSVECTOR2(1);
	TSQuery		query = PG_GETARG_TSQUERY(2);
	float		res;

	res = calc_rank_cd(getWeights(win), txt, query, DEF_NORM_METHOD);

	PG_FREE_IF_COPY(win, 0);
	PG_FREE_IF_COPY(txt, 1);
	PG_FREE_IF_COPY(query, 2);
	PG_RETURN_FLOAT4(res);
}

PG_FUNCTION_INFO_V1(tsvector2_rankcd_ttf);
Datum
tsvector2_rankcd_ttf(PG_FUNCTION_ARGS)
{
	TSVector2	txt = PG_GETARG_TSVECTOR2(0);
	TSQuery		query = PG_GETARG_TSQUERY(1);
	int			method = PG_GETARG_INT32(2);
	float		res;

	res = calc_rank_cd(getWeights(NULL), txt, query, method);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(query, 1);
	PG_RETURN_FLOAT4(res);
}

PG_FUNCTION_INFO_V1(tsvector2_rankcd_tt);
Datum
tsvector2_rankcd_tt(PG_FUNCTION_ARGS)
{
	TSVector2	txt = PG_GETARG_TSVECTOR2(0);
	TSQuery		query = PG_GETARG_TSQUERY(1);
	float		res;

	res = calc_rank_cd(getWeights(NULL), txt, query, DEF_NORM_METHOD);

	PG_FREE_IF_COPY(txt, 0);
	PG_FREE_IF_COPY(query, 1);
	PG_RETURN_FLOAT4(res);
}
