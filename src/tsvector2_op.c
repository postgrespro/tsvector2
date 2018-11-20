/*-------------------------------------------------------------------------
 *
 * tsvector2_op.c
 *	  operations over tsvector2
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 2018, PostgresPro
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <limits.h>

#include "access/htup_details.h"
#include "catalog/namespace.h"
#include "catalog/pg_type.h"
#include "commands/trigger.h"
#include "executor/spi.h"
#include "funcapi.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "parser/parse_coerce.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#if PG_VERSION_NUM >= 100000
#include "utils/regproc.h"
#endif
#include "utils/rel.h"
#include "tsearch/ts_utils.h"

#include "tsvector2.h"

typedef struct
{
	TSVector2	vec;
	int			bidx;
	int			eidx;
	char	   *operand;
} CHKVAL;


typedef struct StatEntry
{
	uint32		ndoc;			/* zero indicates that we were already here
								 * while walking through the tree */
	uint32		nentry;
	struct StatEntry *left;
	struct StatEntry *right;
	uint32		lenlexeme;
	char		lexeme[FLEXIBLE_ARRAY_MEMBER];
} StatEntry;

#define STATENTRYHDRSZ	(offsetof(StatEntry, lexeme))

typedef struct
{
	int32		weight;

	uint32		maxdepth;

	StatEntry **stack;
	uint32		stackpos;

	StatEntry  *root;
} TSVectorStat;

#define STATHDRSIZE (offsetof(TSVectorStat, data))

static int	tsvector2_bsearch(const TSVector2 tsv, char *lexeme, int lexeme_len);

PG_FUNCTION_INFO_V1(tsvector2_match_tsquery);

/*
 * Order: npos, len, word, for all positions (pos, weight)
 */
static int
silly_cmp_tsvector2(const TSVector2 a, const TSVector2 b)
{
	if (VARSIZE(a) < VARSIZE(b))
		return -1;
	else if (VARSIZE(a) > VARSIZE(b))
		return 1;
	else if (a->size < b->size)
		return -1;
	else if (a->size > b->size)
		return 1;
	else
	{
		WordEntry2  *aptr = tsvector2_entries(a);
		WordEntry2  *bptr = tsvector2_entries(b);
		int			i = 0;
		int			res;
		uint32		pos1,
					pos2;

		INITPOS(pos1);
		INITPOS(pos2);

		for (i = 0; i < a->size; i++)
		{
			char	   *lex1 = tsvector2_storage(a) + pos1,
					   *lex2 = tsvector2_storage(b) + pos2;
			int			npos1 = ENTRY_NPOS(a, aptr),
						npos2 = ENTRY_NPOS(b, bptr);
			int			len1 = ENTRY_LEN(a, aptr),
						len2 = ENTRY_LEN(b, bptr);

			if ((npos1 == 0 || npos2 == 0) && npos1 != npos2)
				return npos1 > npos2 ? -1 : 1;
			else if ((res = ts2_compare_string(lex1, len1, lex2, len2, false)) != 0)
			{
				return res;
			}
			else if (npos1 > 0)
			{
				WordEntryPos *ap,
						   *bp;
				int			j;

				ap = get_lexeme_positions(lex1, len1);
				bp = get_lexeme_positions(lex2, len2);

				if (npos1 != npos2)
					return (npos1 > npos2) ? -1 : 1;

				for (j = 0; j < npos1; j++)
				{
					if (WEP_GETPOS(*ap) != WEP_GETPOS(*bp))
					{
						return (WEP_GETPOS(*ap) > WEP_GETPOS(*bp)) ? -1 : 1;
					}
					else if (WEP_GETWEIGHT(*ap) != WEP_GETWEIGHT(*bp))
					{
						return (WEP_GETWEIGHT(*ap) > WEP_GETWEIGHT(*bp)) ? -1 : 1;
					}
					ap++, bp++;
				}
			}

			INCRPTR(a, aptr, pos1);
			INCRPTR(b, bptr, pos2);
		}
	}

	return 0;
}

#define TSVECTOR2CMPFUNC( type, action, ret )			\
PG_FUNCTION_INFO_V1(tsvector2_##type);					\
Datum													\
tsvector2_##type(PG_FUNCTION_ARGS)						\
{														\
	TSVector2	a = PG_GETARG_TSVECTOR2(0);				\
	TSVector2	b = PG_GETARG_TSVECTOR2(1);				\
	int			res = silly_cmp_tsvector2(a, b);			\
	PG_FREE_IF_COPY(a,0);								\
	PG_FREE_IF_COPY(b,1);								\
	PG_RETURN_##ret( res action 0 );					\
}	\
/* keep compiler quiet - no extra ; */					\
extern int no_such_variable

TSVECTOR2CMPFUNC(lt, <, BOOL);
TSVECTOR2CMPFUNC(le, <=, BOOL);
TSVECTOR2CMPFUNC(eq, ==, BOOL);
TSVECTOR2CMPFUNC(ge, >=, BOOL);
TSVECTOR2CMPFUNC(gt, >, BOOL);
TSVECTOR2CMPFUNC(ne, !=, BOOL);
TSVECTOR2CMPFUNC(cmp, +, INT32);

PG_FUNCTION_INFO_V1(tsvector2_strip);
Datum
tsvector2_strip(PG_FUNCTION_ARGS)
{
	TSVector2	in = PG_GETARG_TSVECTOR2(0);
	TSVector2	out;
	int			i,
				count,
				posout = 0,
				pos,
				len = 0;
	WordEntry2  *entryin = tsvector2_entries(in);

	count = in->size;
	for (i = 0; i < count; i++)
		INCRSIZE(len, i, ENTRY_LEN(in, tsvector2_entries(in) + i), 0);

	len = CALCDATASIZE(count, len);
	out = (TSVector2) palloc0(len);
	SET_VARSIZE(out, len);
	out->size = count;

	INITPOS(pos);
	for (i = 0; i < count; i++)
	{
		tsvector2_addlexeme(out, i, &posout,
						   tsvector2_storage(in) + pos, ENTRY_LEN(in, entryin),
						   NULL, 0);

		INCRPTR(in, entryin, pos);
	}

	PG_FREE_IF_COPY(in, 0);
	PG_RETURN_POINTER(out);
}

PG_FUNCTION_INFO_V1(tsvector2_length);
Datum
tsvector2_length(PG_FUNCTION_ARGS)
{
	TSVector2	in = PG_GETARG_TSVECTOR2(0);
	int32		ret = in->size;

	PG_FREE_IF_COPY(in, 0);
	PG_RETURN_INT32(ret);
}

PG_FUNCTION_INFO_V1(tsvector2_setweight);
Datum
tsvector2_setweight(PG_FUNCTION_ARGS)
{
	TSVector2	in = PG_GETARG_TSVECTOR2(0);
	char		cw = PG_GETARG_CHAR(1);
	TSVector2	out;
	int			i;
	WordEntry2  *weptr;
	int			w = 0;
	uint32		pos;

	switch (cw)
	{
		case 'A':
		case 'a':
			w = 3;
			break;
		case 'B':
		case 'b':
			w = 2;
			break;
		case 'C':
		case 'c':
			w = 1;
			break;
		case 'D':
		case 'd':
			w = 0;
			break;
		default:
			/* internal error */
			elog(ERROR, "unrecognized weight: %d", cw);
	}

	out = (TSVector2) palloc(VARSIZE(in));
	memcpy(out, in, VARSIZE(in));
	weptr = tsvector2_entries(out);

	INITPOS(pos);
	for (i = 0; i < out->size; i++)
	{
		int			j,
					npos = ENTRY_NPOS(out, weptr);

		if (npos)
		{
			WordEntryPos *p = get_lexeme_positions(tsvector2_storage(out) + pos,
					ENTRY_LEN(out, weptr));

			for (j = 0; j < npos; j++)
				WEP_SETWEIGHT(p[j], w);
		}
		INCRPTR(out, weptr, pos);
	}

	PG_FREE_IF_COPY(in, 0);
	PG_RETURN_POINTER(out);
}

/*
 * setweight(tsin tsvector2, char_weight "char", lexemes "text"[])
 *
 * Assign weight w to elements of tsin that are listed in lexemes.
 */
PG_FUNCTION_INFO_V1(tsvector2_setweight_by_filter);
Datum
tsvector2_setweight_by_filter(PG_FUNCTION_ARGS)
{
	TSVector2	tsin = PG_GETARG_TSVECTOR2(0);
	char		char_weight = PG_GETARG_CHAR(1);
	ArrayType  *lexemes = PG_GETARG_ARRAYTYPE_P(2);

	TSVector2	tsout;
	int			i,
				nlexemes,
				weight;
	Datum	   *dlexemes;
	bool	   *nulls;

	switch (char_weight)
	{
		case 'A':
		case 'a':
			weight = 3;
			break;
		case 'B':
		case 'b':
			weight = 2;
			break;
		case 'C':
		case 'c':
			weight = 1;
			break;
		case 'D':
		case 'd':
			weight = 0;
			break;
		default:
			/* internal error */
			elog(ERROR, "unrecognized weight: %c", char_weight);
	}

	tsout = (TSVector2) palloc(VARSIZE(tsin));
	memcpy(tsout, tsin, VARSIZE(tsin));
	deconstruct_array(lexemes, TEXTOID, -1, false, 'i',
					  &dlexemes, &nulls, &nlexemes);

	/*
	 * Assuming that lexemes array is significantly shorter than tsvector2 we
	 * can iterate through lexemes performing binary search of each lexeme
	 * from lexemes in tsvector2.
	 */
	for (i = 0; i < nlexemes; i++)
	{
		char	   *lex;
		int			lex_len,
					lex_idx,
					npos;

		if (nulls[i])
			ereport(ERROR,
					(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
					 errmsg("lexeme array may not contain nulls")));

		lex = VARDATA(dlexemes[i]);
		lex_len = VARSIZE(dlexemes[i]) - VARHDRSZ;
		lex_idx = tsvector2_bsearch(tsin, lex, lex_len);
		npos = ENTRY_NPOS(tsin, tsvector2_entries(tsout) + lex_idx);

		if (lex_idx >= 0 && npos > 0)
		{
			int			j;
			WordEntry2  *we;
			char	   *lexeme = tsvector2_getlexeme(tsout, lex_idx, &we);

			WordEntryPos *p = get_lexeme_positions(lexeme, we->len);

			for (j = 0; j < npos; j++)
				WEP_SETWEIGHT(p[j], weight);
		}
	}

	PG_FREE_IF_COPY(tsin, 0);
	PG_FREE_IF_COPY(lexemes, 2);

	PG_RETURN_POINTER(tsout);
}

#define compareEntry(pa, a, pb, b) \
	ts2_compare_string((pa) + (a)->pos, (a)->len,	\
					(pb) + (b)->pos, (b)->len,	\
					false)

/*
 * Add positions from src to dest after offsetting them by maxpos.
 * Return the number added (might be less than expected due to overflow)
 */
static int32
add_pos(char *src, WordEntry2 *srcptr,
		WordEntryPos *dest, int from,
		int32 maxpos)
{
	uint16		clen = from;
	int			i;
	uint16		slen = srcptr->npos;
	WordEntryPos *spos = get_lexeme_positions(src, srcptr->len);

	Assert(!srcptr->hasoff);
	for (i = 0;
		 i < slen && clen < MAXNUMPOS &&
		 (clen == 0 || WEP_GETPOS(dest[clen - 1]) != MAXENTRYPOS - 1);
		 i++)
	{
		WEP_SETWEIGHT(dest[clen], WEP_GETWEIGHT(spos[i]));
		WEP_SETPOS(dest[clen], LIMITPOS(WEP_GETPOS(spos[i]) + maxpos));
		clen++;
	}

	return clen - from;
}

/*
 * Perform binary search of given lexeme in TSVector2.
 * Returns lexeme position in TSVector2's entry array or -1 if lexeme wasn't
 * found.
 */
static int
tsvector2_bsearch(const TSVector2 tsv, char *lexeme, int lexeme_len)
{
	int			StopLow = 0,
				StopHigh = tsv->size,
				StopMiddle,
				cmp;

	while (StopLow < StopHigh)
	{
		WordEntry2  *entry = NULL;
		char	   *str;

		StopMiddle = (StopLow + StopHigh) / 2;
		str = tsvector2_getlexeme(tsv, StopMiddle, &entry);
		cmp = ts2_compare_string(lexeme, lexeme_len,
							  str, entry->len, false);

		if (cmp < 0)
			StopHigh = StopMiddle;
		else if (cmp > 0)
			StopLow = StopMiddle + 1;
		else					/* found it */
			return StopMiddle;
	}

	return -1;
}

/*
 * qsort comparator functions
 */

static int
compare_int(const void *va, const void *vb)
{
	int			a = *((const int *) va);
	int			b = *((const int *) vb);

	if (a == b)
		return 0;
	return (a > b) ? 1 : -1;
}

static int
compare_text_lexemes(const void *va, const void *vb)
{
	Datum		a = *((const Datum *) va);
	Datum		b = *((const Datum *) vb);
	char	   *alex = VARDATA_ANY(a);
	int			alex_len = VARSIZE_ANY_EXHDR(a);
	char	   *blex = VARDATA_ANY(b);
	int			blex_len = VARSIZE_ANY_EXHDR(b);

	return ts2_compare_string(alex, alex_len, blex, blex_len, false);
}

/*
 * Internal routine to delete lexemes from TSVector2 by array of offsets.
 *
 * int *indices_to_delete -- array of lexeme offsets to delete (modified here!)
 * int indices_count -- size of that array
 *
 * Returns new TSVector2 without given lexemes along with their positions
 * and weights.
 */
static TSVector2
tsvector2_delete_by_indices(TSVector2 tsv, int *indices_to_delete,
						   int indices_count)
{
	TSVector2	tsout;
	WordEntry2  *ptr = tsvector2_entries(tsv);
	int			i,				/* index in input tsvector2 */
				j,				/* index in output tsvector2 */
				k,				/* index in indices_to_delete */
				curoff = 0,		/* index in data area of output */
				pos;

	/*
	 * Sort the filter array to simplify membership checks below.  Also, get
	 * rid of any duplicate entries, so that we can assume that indices_count
	 * is exactly equal to the number of lexemes that will be removed.
	 */
	if (indices_count > 1)
	{
		int			kp;

		qsort(indices_to_delete, indices_count, sizeof(int), compare_int);
		kp = 0;
		for (k = 1; k < indices_count; k++)
		{
			if (indices_to_delete[k] != indices_to_delete[kp])
				indices_to_delete[++kp] = indices_to_delete[k];
		}
		indices_count = ++kp;
	}

	/*
	 * Here we overestimate tsout size, since we don't know how much space is
	 * used by the deleted lexeme(s).  We will set exact size below.
	 */
	tsout = (TSVector2) palloc0(VARSIZE(tsv));

	/* This count must be correct because tsvector2_storage(tsout) relies on it. */
	tsout->size = tsv->size - indices_count;

	/*
	 * Copy tsv to tsout, skipping lexemes listed in indices_to_delete.
	 */

	INITPOS(pos);
	for (i = j = k = 0; i < tsv->size; i++)
	{
		char	   *lex = tsvector2_storage(tsv) + pos;
		int			lex_len = ENTRY_LEN(tsv, ptr);

		/*
		 * If current i is present in indices_to_delete, skip this lexeme.
		 * Since indices_to_delete is already sorted, we only need to check
		 * the current (k'th) entry.
		 */
		if (k < indices_count && i == indices_to_delete[k])
		{
			k++;
			goto next;
		}

		tsvector2_addlexeme(tsout, j++, &curoff, lex, lex_len,
						   get_lexeme_positions(lex, lex_len), ENTRY_NPOS(tsv, ptr));

next:
		INCRPTR(tsv, ptr, pos);
	}

	/*
	 * k should now be exactly equal to indices_count. If it isn't then the
	 * caller provided us with indices outside of [0, tsv->size) range and
	 * estimation of tsout's size is wrong.
	 */
	Assert(k == indices_count);
	SET_VARSIZE(tsout, CALCDATASIZE(tsout->size, curoff));
	return tsout;
}

/*
 * Delete given lexeme from tsvector2.
 * Implementation of user-level ts_delete(tsvector2, text).
 */
PG_FUNCTION_INFO_V1(tsvector2_delete_str);
Datum
tsvector2_delete_str(PG_FUNCTION_ARGS)
{
	TSVector2	tsin = PG_GETARG_TSVECTOR2(0),
				tsout;
	text	   *tlexeme = PG_GETARG_TEXT_PP(1);
	char	   *lexeme = VARDATA_ANY(tlexeme);
	int			lexeme_len = VARSIZE_ANY_EXHDR(tlexeme),
				skip_index;

	if ((skip_index = tsvector2_bsearch(tsin, lexeme, lexeme_len)) == -1)
		PG_RETURN_POINTER(tsin);

	tsout = tsvector2_delete_by_indices(tsin, &skip_index, 1);

	PG_FREE_IF_COPY(tsin, 0);
	PG_FREE_IF_COPY(tlexeme, 1);
	PG_RETURN_POINTER(tsout);
}

/*
 * Delete given array of lexemes from tsvector2.
 * Implementation of user-level ts_delete(tsvector2, text[]).
 */
PG_FUNCTION_INFO_V1(tsvector2_delete_arr);
Datum
tsvector2_delete_arr(PG_FUNCTION_ARGS)
{
	TSVector2	tsin = PG_GETARG_TSVECTOR2(0),
				tsout;
	ArrayType  *lexemes = PG_GETARG_ARRAYTYPE_P(1);
	int			i,
				nlex,
				skip_count,
			   *skip_indices;
	Datum	   *dlexemes;
	bool	   *nulls;

	deconstruct_array(lexemes, TEXTOID, -1, false, 'i',
					  &dlexemes, &nulls, &nlex);

	/*
	 * In typical use case array of lexemes to delete is relatively small. So
	 * here we optimize things for that scenario: iterate through lexarr
	 * performing binary search of each lexeme from lexarr in tsvector2.
	 */
	skip_indices = palloc0(nlex * sizeof(int));
	for (i = skip_count = 0; i < nlex; i++)
	{
		char	   *lex;
		int			lex_len,
					lex_pos;

		if (nulls[i])
			ereport(ERROR,
					(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
					 errmsg("lexeme array may not contain nulls")));

		lex = VARDATA(dlexemes[i]);
		lex_len = VARSIZE(dlexemes[i]) - VARHDRSZ;
		lex_pos = tsvector2_bsearch(tsin, lex, lex_len);

		if (lex_pos >= 0)
			skip_indices[skip_count++] = lex_pos;
	}

	tsout = tsvector2_delete_by_indices(tsin, skip_indices, skip_count);

	pfree(skip_indices);
	PG_FREE_IF_COPY(tsin, 0);
	PG_FREE_IF_COPY(lexemes, 1);

	PG_RETURN_POINTER(tsout);
}

/*
 * Expand tsvector2 as table with following columns:
 *	   lexeme: lexeme text
 *	   positions: integer array of lexeme positions
 *	   weights: char array of weights corresponding to positions
 */
PG_FUNCTION_INFO_V1(tsvector2_unnest);
Datum
tsvector2_unnest(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	TSVector2	tsin;
	uint32		pos;

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		TupleDesc	tupdesc;

		funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		tupdesc = CreateTemplateTupleDesc(3, false);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "lexeme",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "positions",
						   INT2ARRAYOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "weights",
						   TEXTARRAYOID, -1, 0);
		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		INITPOS(pos);
		funcctx->user_fctx = list_make2(PG_GETARG_TSVECTOR2(0), makeInteger(pos));

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();
	tsin = (TSVector2) linitial(funcctx->user_fctx);
	pos = intVal(lsecond(funcctx->user_fctx));

	if (funcctx->call_cntr < tsin->size)
	{
		WordEntry2  *entry = tsvector2_entries(tsin) + funcctx->call_cntr;
		char	   *data = tsvector2_storage(tsin);
		HeapTuple	tuple;
		int			j,
					npos = ENTRY_NPOS(tsin, entry),
					lex_len = ENTRY_LEN(tsin, entry);
		bool		nulls[] = {false, false, false};
		Datum		values[3];

		values[0] = PointerGetDatum(
									cstring_to_text_with_len(data + pos, lex_len));

		if (npos)
		{
			WordEntryPos *apos = get_lexeme_positions(data + pos, lex_len);
			Datum	   *positions;
			Datum	   *weights;
			char		weight;

			/*
			 * Internally tsvector2 stores position and weight in the same
			 * uint16 (2 bits for weight, 14 for position). Here we extract
			 * that in two separate arrays.
			 */
			positions = palloc(npos * sizeof(Datum));
			weights = palloc(npos * sizeof(Datum));
			for (j = 0; j < npos; j++)
			{
				positions[j] = Int16GetDatum(WEP_GETPOS(apos[j]));
				weight = 'D' - WEP_GETWEIGHT(apos[j]);
				weights[j] = PointerGetDatum(
											 cstring_to_text_with_len(&weight, 1)
					);
			}

			values[1] = PointerGetDatum(
										construct_array(positions, npos, INT2OID, 2, true, 's'));
			values[2] = PointerGetDatum(
										construct_array(weights, npos, TEXTOID, -1, false, 'i'));
		}
		else
		{
			nulls[1] = nulls[2] = true;
		}

		INCRPTR(tsin, entry, intVal(lsecond(funcctx->user_fctx)));
		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
	}
	else
	{
		pfree(tsin);
		SRF_RETURN_DONE(funcctx);
	}
}

/*
 * Convert tsvector2 to array of lexemes.
 */
PG_FUNCTION_INFO_V1(tsvector2_to_array);
Datum
tsvector2_to_array(PG_FUNCTION_ARGS)
{
	TSVector2	tsin = PG_GETARG_TSVECTOR2(0);
	WordEntry2  *entry = tsvector2_entries(tsin);
	Datum	   *elements;
	int			i;
	ArrayType  *array;
	long		pos;

	elements = palloc(tsin->size * sizeof(Datum));

	INITPOS(pos);
	for (i = 0; i < tsin->size; i++)
	{
		elements[i] = PointerGetDatum(
									  cstring_to_text_with_len(tsvector2_storage(tsin) + pos, ENTRY_LEN(tsin, entry)));
		INCRPTR(tsin, entry, pos);
	}

	array = construct_array(elements, tsin->size, TEXTOID, -1, false, 'i');

	pfree(elements);
	PG_FREE_IF_COPY(tsin, 0);
	PG_RETURN_POINTER(array);
}

/*
 * Returns offset by given index in TSVector2,
 * this function used when we need random access
 */
int
tsvector2_getoffset(TSVector2 vec, int idx, WordEntry2 **we)
{
	int			offset = 0;
	WordEntry2  *entry;

	entry = tsvector2_entries(vec) + idx;
	if (we)
		*we = entry;

	while (!entry->hasoff)
	{
		entry--;
		if (!entry->hasoff)
			offset += SHORTALIGN(entry->len) + entry->npos * sizeof(WordEntryPos);
	}

	Assert(entry >= tsvector2_entries(vec));

	if (idx % TS_OFFSET_STRIDE)
	{
		/* if idx is by offset */
		WordEntry2  *offset_entry = (WordEntry2 *) (tsvector2_storage(vec) + entry->offset);

		offset += entry->offset + sizeof(WordEntry2);
		offset += SHORTALIGN(offset_entry->len) + offset_entry->npos * sizeof(WordEntryPos);
	}
	else
	{
		Assert(entry == tsvector2_entries(vec) + idx);

		if (we)
			*we = (WordEntry2 *) (tsvector2_storage(vec) + entry->offset);
		offset = entry->offset + sizeof(WordEntry2);
	}

	return offset;
}

/*
 * Add lexeme and its positions to tsvector2 and move dataoff (offset where
 * data should be added) to new position.
 * Returns pointer to lexeme start
 */
char *
tsvector2_addlexeme(TSVector2 tsv, int idx, int *dataoff,
				   char *lexeme, int lexeme_len, WordEntryPos *pos, int npos)
{
	int			stroff;
	WordEntry2  *entry;
	char	   *result;

	/* when idx is 0, dataoff should be 0 too, and otherwise */
	Assert(!((idx == 0) ^ (*dataoff == 0)));

	stroff = *dataoff;
	entry = tsvector2_entries(tsv) + idx;

	if (idx % TS_OFFSET_STRIDE == 0)
	{
		/* WordEntry2 with offset */
		WordEntry2	offentry;

		stroff = INTALIGN(stroff);
		entry->hasoff = 1;
		entry->offset = stroff;

		/* fill WordEntry2 for offset */
		offentry.hasoff = 0;
		offentry.len = lexeme_len;
		offentry.npos = npos;
		memcpy(tsvector2_storage(tsv) + stroff, &offentry, sizeof(WordEntry2));
		stroff += sizeof(WordEntry2);
	}
	else
	{
		stroff = SHORTALIGN(stroff);
		entry->hasoff = 0;
		entry->len = lexeme_len;
		entry->npos = npos;
	}

	memcpy(tsvector2_storage(tsv) + stroff, lexeme, lexeme_len);
	result = tsvector2_storage(tsv) + stroff;
	stroff += lexeme_len;

	if (npos)
	{
		if (npos > 0xFFFF)
			elog(ERROR, "positions array too long");

		/*
		 * Pad to 2-byte alignment if necessary. We don't know how memory was
		 * allocated, so in case of aligning we need to make sure that unused
		 * is zero.
		 */
		if (stroff != SHORTALIGN(stroff))
		{
			*(tsvector2_storage(tsv) + stroff) = '\0';
			stroff = SHORTALIGN(stroff);
		}

		/* Copy positions */
		if (pos)
			memcpy(tsvector2_storage(tsv) + stroff, pos, npos * sizeof(WordEntryPos));

		stroff += npos * sizeof(WordEntryPos);
	}

	*dataoff = stroff;

	return result;
}

/*
 * Build tsvector2 from array of lexemes.
 */
PG_FUNCTION_INFO_V1(array_to_tsvector2);
Datum
array_to_tsvector2(PG_FUNCTION_ARGS)
{
	ArrayType  *v = PG_GETARG_ARRAYTYPE_P(0);
	TSVector2	tsout;
	Datum	   *dlexemes;
	bool	   *nulls;
	int			nitems,
				i,
				j,
				tslen,
				cur = 0,
				datalen = 0;

	deconstruct_array(v, TEXTOID, -1, false, 'i', &dlexemes, &nulls, &nitems);

	/* Reject nulls (maybe we should just ignore them, instead?) */
	for (i = 0; i < nitems; i++)
	{
		if (nulls[i])
			ereport(ERROR,
					(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
					 errmsg("lexeme array may not contain nulls")));
	}

	/* Sort and de-dup, because this is required for a valid tsvector2. */
	if (nitems > 1)
	{
		qsort(dlexemes, nitems, sizeof(Datum), compare_text_lexemes);
		j = 0;
		for (i = 1; i < nitems; i++)
		{
			if (compare_text_lexemes(&dlexemes[j], &dlexemes[i]) < 0)
				dlexemes[++j] = dlexemes[i];
		}
		nitems = ++j;
	}

	/* Calculate space needed for surviving lexemes. */
	for (i = 0; i < nitems; i++)
	{
		int			lex_len = VARSIZE(dlexemes[i]) - VARHDRSZ;

		INCRSIZE(datalen, i, lex_len, 0);
	}
	tslen = CALCDATASIZE(nitems, datalen);

	/* Allocate and fill tsvector2. */
	tsout = (TSVector2) palloc0(tslen);
	SET_VARSIZE(tsout, tslen);
	tsout->size = nitems;

	for (i = 0; i < nitems; i++)
	{
		char	   *lex = VARDATA(dlexemes[i]);
		int			lex_len = VARSIZE(dlexemes[i]) - VARHDRSZ;

		tsvector2_addlexeme(tsout, i, &cur, lex, lex_len, NULL, 0);
	}

	PG_FREE_IF_COPY(v, 0);
	PG_RETURN_POINTER(tsout);
}

/*
 * ts_filter(): keep only lexemes with given weights in tsvector2.
 */
PG_FUNCTION_INFO_V1(tsvector2_filter);
Datum
tsvector2_filter(PG_FUNCTION_ARGS)
{
	TSVector2	tsin = PG_GETARG_TSVECTOR2(0),
				tsout;
	ArrayType  *weights = PG_GETARG_ARRAYTYPE_P(1);
	char	   *dataout;
	Datum	   *dweights;
	bool	   *nulls;
	int			nweights;
	int			i,
				j,
				dataoff = 0,
				pos;
	char		mask = 0;
	WordEntry2  *ptr = tsvector2_entries(tsin);

	deconstruct_array(weights, CHAROID, 1, true, 'c',
					  &dweights, &nulls, &nweights);

	for (i = 0; i < nweights; i++)
	{
		char		char_weight;

		if (nulls[i])
			ereport(ERROR,
					(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
					 errmsg("weight array may not contain nulls")));

		char_weight = DatumGetChar(dweights[i]);
		switch (char_weight)
		{
			case 'A':
			case 'a':
				mask = mask | 8;
				break;
			case 'B':
			case 'b':
				mask = mask | 4;
				break;
			case 'C':
			case 'c':
				mask = mask | 2;
				break;
			case 'D':
			case 'd':
				mask = mask | 1;
				break;
			default:
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("unrecognized weight: \"%c\"", char_weight)));
		}
	}

	tsout = (TSVector2) palloc0(VARSIZE(tsin));
	tsout->size = tsin->size;
	dataout = tsvector2_storage(tsout);

	INITPOS(pos);
	for (i = j = 0; i < tsin->size; i++)
	{
		WordEntryPos *posin,
				   *posout;
		int			k,
					npos = 0,
					lex_len = ENTRY_LEN(tsin, ptr);
		char	   *lex = tsvector2_storage(tsin) + pos,
				   *lexout;

		posin = get_lexeme_positions(lex, lex_len);
		for (k = 0; k < ENTRY_NPOS(tsin, ptr); k++)
		{
			if (mask & (1 << WEP_GETWEIGHT(posin[k])))
				npos++;
		}

		if (!npos)
			goto next;

		lexout = tsvector2_addlexeme(tsout, j++, &dataoff, lex, lex_len,
									NULL, npos);
		posout = get_lexeme_positions(lexout, lex_len);
		npos = 0;
		for (k = 0; k < ENTRY_NPOS(tsin, ptr); k++)
		{
			if (mask & (1 << WEP_GETWEIGHT(posin[k])))
				posout[npos++] = posin[k];
		}

next:
		INCRPTR(tsin, ptr, pos);
	}

	tsout->size = j;
	if (dataout != tsvector2_storage(tsout))
		memmove(tsvector2_storage(tsout), dataout, dataoff);

	SET_VARSIZE(tsout, CALCDATASIZE(tsout->size, dataoff));

	PG_FREE_IF_COPY(tsin, 0);
	PG_RETURN_POINTER(tsout);
}

/* Get max position in in1; we'll need this to offset in2's positions */
static int
get_maxpos(TSVector2 tsv)
{
	int			i,
				j,
				maxpos = 0;
	WordEntry2  *ptr = tsvector2_entries(tsv);
	uint32		pos;
	WordEntryPos *apos;

	INITPOS(pos);
	for (i = 0; i < tsv->size; i++)
	{
		apos = get_lexeme_positions(tsvector2_storage(tsv) + pos, ENTRY_LEN(tsv, ptr));
		for (j = 0; j < ENTRY_NPOS(tsv, ptr); j++)
		{
			if (WEP_GETPOS(apos[j]) > maxpos)
				maxpos = WEP_GETPOS(apos[j]);
		}

		INCRPTR(tsv, ptr, pos);
	}

	return maxpos;
}

PG_FUNCTION_INFO_V1(tsvector2_concat);
Datum
tsvector2_concat(PG_FUNCTION_ARGS)
{
	TSVector2	in1 = PG_GETARG_TSVECTOR2(0),
				in2 = PG_GETARG_TSVECTOR2(1),
				out;
	WordEntry2  *ptr,
			   *ptr1,
			   *ptr2;
	int			maxpos = 0,
				i,
				i1,
				i2,
				output_bytes,
				pos1,
				pos2,
				dataoff;
	char	   *data;

	ptr1 = tsvector2_entries(in1);
	ptr2 = tsvector2_entries(in2);
	i1 = in1->size;
	i2 = in2->size;

	/*
	 * Conservative estimate of space needed.  We might need all the data in
	 * both inputs, and conceivably add a pad bytes before lexeme and position
	 * data, and pad bytes before WordEntry2 for offset entry.
	 */
	output_bytes = VARSIZE(in1) + VARSIZE(in2) + i1 * 2 + i2 * 2;
	output_bytes += 4 * (i1 + i2) / TS_OFFSET_STRIDE;

	out = (TSVector2) palloc0(output_bytes);
	SET_VARSIZE(out, output_bytes);

	/*
	 * We must make out->size valid so that tsvector2_storage(out) is sensible.  We'll
	 * collapse out any unused space at the end.
	 */
	out->size = i1 + i2;

	ptr = NULL;
	data = tsvector2_storage(out);
	i = 0;
	dataoff = 0;

	INITPOS(pos1);
	INITPOS(pos2);

	/*
	 * we will need max position from first tsvector2 to add it positions of
	 * second tsvector2
	 */
	maxpos = get_maxpos(in1);

	while (i1 && i2)
	{
		char	   *lex = tsvector2_storage(in1) + pos1,
				   *lex2 = tsvector2_storage(in2) + pos2;

		int			lex_len = ENTRY_LEN(in1, ptr1),
					lex2_len = ENTRY_LEN(in2, ptr2);

		int			cmp = ts2_compare_string(lex, lex_len, lex2, lex2_len, false);

		if (cmp < 0)
		{						/* in1 first */
			tsvector2_addlexeme(out, i, &dataoff,
							   lex, lex_len,
							   get_lexeme_positions(lex, lex_len), ENTRY_NPOS(in1, ptr1));

			INCRPTR(in1, ptr1, pos1);
			i1--;
			i++;
		}
		else if (cmp > 0)
		{						/* in2 first */
			char	   *new_lex;
			WordEntry2  *we = UNWRAP_ENTRY(in2, ptr2);

			new_lex = tsvector2_addlexeme(out, i, &dataoff, lex2, lex2_len, NULL, 0);
			if (we->npos > 0)
			{
				int			addlen;
				WordEntryPos *apos = get_lexeme_positions(new_lex, lex2_len);

				addlen = add_pos(lex2, we, apos, 0, maxpos);
				if (addlen > 0)
				{
					ptr = UNWRAP_ENTRY(out, tsvector2_entries(out) + i);
					ptr->npos = addlen;
					dataoff = SHORTALIGN(dataoff);
					dataoff += ptr->npos * sizeof(WordEntryPos);
				}
			}

			INCRPTR(in2, ptr2, pos2);
			i++;
			i2--;
		}
		else
		{
			char	   *new_lex;
			int			npos1 = ENTRY_NPOS(in1, ptr1),
						npos2 = ENTRY_NPOS(in2, ptr2);
			WordEntryPos *apos;

			new_lex = tsvector2_addlexeme(out, i, &dataoff, lex, lex_len, NULL, 0);
			apos = get_lexeme_positions(new_lex, lex_len);

			if (npos1 || npos2)
			{
				int			addlen;
				char	   *lex2 = tsvector2_storage(in2) + pos2;

				ptr = UNWRAP_ENTRY(out, tsvector2_entries(out) + i);
				if (npos1)
				{
					/* add positions from left tsvector2 */
					addlen = add_pos(lex, UNWRAP_ENTRY(in1, ptr1), apos, 0, 0);
					ptr->npos = addlen;

					if (npos2)
					{
						/* add positions from right right tsvector2 */
						addlen = add_pos(lex2, UNWRAP_ENTRY(in2, ptr2), apos, addlen, maxpos);
						ptr->npos += addlen;
					}
				}
				else			/* npos in second should be > 0 */
				{
					/* add positions from right tsvector2 */
					addlen = add_pos(lex2, UNWRAP_ENTRY(in2, ptr2), apos, 0, maxpos);
					ptr->npos = addlen;
				}

				dataoff = SHORTALIGN(dataoff);
				dataoff += ptr->npos * sizeof(WordEntryPos);
			}

			INCRPTR(in1, ptr1, pos1);
			INCRPTR(in2, ptr2, pos2);
			i++;
			i1--;
			i2--;
		}
	}

	while (i1)
	{
		char	   *lex = tsvector2_storage(in1) + pos1;
		int			lex_len = ENTRY_LEN(in1, ptr1);

		tsvector2_addlexeme(out, i, &dataoff,
						   lex, lex_len,
						   get_lexeme_positions(lex, lex_len), ENTRY_NPOS(in1, ptr1));

		INCRPTR(in1, ptr1, pos1);
		i++;
		i1--;
	}

	while (i2)
	{
		char	   *lex = tsvector2_storage(in2) + pos2,
				   *new_lex;
		int			lex_len = ENTRY_LEN(in2, ptr2),
					npos = ENTRY_NPOS(in2, ptr2);

		new_lex = tsvector2_addlexeme(out, i, &dataoff, lex, lex_len, NULL, 0);
		if (npos > 0)
		{
			int			addlen;
			WordEntryPos *apos = get_lexeme_positions(new_lex, lex_len);

			addlen = add_pos(lex, UNWRAP_ENTRY(in2, ptr2), apos, 0, maxpos);
			if (addlen > 0)
			{
				WordEntry2  *ptr = UNWRAP_ENTRY(out, tsvector2_entries(out) + i);

				ptr->npos = addlen;
				dataoff = SHORTALIGN(dataoff);
				dataoff += npos * sizeof(WordEntryPos);
			}
		}

		INCRPTR(in2, ptr2, pos2);
		i++;
		i2--;
	}

	/*
	 * Instead of checking each offset individually, we check for overflow of
	 * pos fields once at the end.
	 */
	if (dataoff > MAXSTRPOS)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("string is too long for tsvector2 (%d bytes, max %d bytes)", dataoff, MAXSTRPOS)));

	/*
	 * Adjust sizes (asserting that we didn't overrun the original estimates)
	 * and collapse out any unused array entries.
	 */
	out->size = i;
	if (data != tsvector2_storage(out))
		memmove(tsvector2_storage(out), data, dataoff);
	output_bytes = CALCDATASIZE(out->size, dataoff);
	Assert(output_bytes <= VARSIZE(out));
	SET_VARSIZE(out, output_bytes);

	PG_FREE_IF_COPY(in1, 0);
	PG_FREE_IF_COPY(in2, 1);
	PG_RETURN_POINTER(out);
}

/*
 * Compare two strings by tsvector2 rules.
 *
 * if isPrefix = true then it returns zero value iff b has prefix a
 */
int32
ts2_compare_string(char *a, int lena, char *b, int lenb, bool prefix)
{
	int			cmp;

	if (lena == 0)
	{
		if (prefix)
			cmp = 0;			/* empty string is prefix of anything */
		else
			cmp = (lenb > 0) ? -1 : 0;
	}
	else if (lenb == 0)
	{
		cmp = (lena > 0) ? 1 : 0;
	}
	else
	{
		cmp = memcmp(a, b, Min(lena, lenb));

		if (prefix)
		{
			if (cmp == 0 && lena > lenb)
				cmp = 1;		/* a is longer, so not a prefix of b */
		}
		else if (cmp == 0 && lena != lenb)
		{
			cmp = (lena < lenb) ? -1 : 1;
		}
	}

	return cmp;
}

/*
 * Check weight info or/and fill 'data' with the required positions
 */
static bool
checkclass_str(WordEntryPos *pv, int npos, QueryOperand *val,
			   ExecPhraseData *data)
{
	bool		result = false;

	if (npos && (val->weight || data))
	{
		if (val->weight && data)
		{
			WordEntryPos *posvec_iter = pv;
			WordEntryPos *dptr;

			/*
			 * Filter position information by weights
			 */
			dptr = data->pos = palloc(sizeof(WordEntryPos) * npos);
			data->allocated = true;

			/* Is there a position with a matching weight? */
			while (posvec_iter < (pv + npos))
			{
				/* If true, append this position to the data->pos */
				if (val->weight & (1 << WEP_GETWEIGHT(*posvec_iter)))
				{
					*dptr = WEP_GETPOS(*posvec_iter);
					dptr++;
				}

				posvec_iter++;
			}

			data->npos = dptr - data->pos;

			if (data->npos > 0)
				result = true;
		}
		else if (val->weight)
		{
			WordEntryPos *posvec_iter = pv;

			/* Is there a position with a matching weight? */
			while (posvec_iter < (pv + npos))
			{
				if (val->weight & (1 << WEP_GETWEIGHT(*posvec_iter)))
				{
					result = true;
					break;		/* no need to go further */
				}

				posvec_iter++;
			}
		}
		else					/* data != NULL */
		{
			data->npos = npos;
			data->pos = pv;
			data->allocated = false;
			result = true;
		}
	}
	else
	{
		result = true;
	}

	return result;
}

/*
 * Removes duplicate pos entries. We can't use uniquePos() from
 * tsvector2.c because array might be longer than MAXENTRYPOS
 *
 * Returns new length.
 */
static int
uniqueLongPos(WordEntryPos *pos, int npos)
{
	WordEntryPos *pos_iter,
			   *result;

	if (npos <= 1)
		return npos;

	qsort((void *) pos, npos, sizeof(WordEntryPos), compareWordEntryPos);

	result = pos;
	pos_iter = pos + 1;
	while (pos_iter < pos + npos)
	{
		if (WEP_GETPOS(*pos_iter) != WEP_GETPOS(*result))
		{
			result++;
			*result = WEP_GETPOS(*pos_iter);
		}

		pos_iter++;
	}

	return result + 1 - pos;
}

/*
 * is there value 'val' in array or not ?
 */
static bool
checkcondition_str(void *checkval, QueryOperand *val, ExecPhraseData *data)
{
	CHKVAL	   *chkval = (CHKVAL *) checkval;
	int			StopLow = chkval->bidx;
	int			StopHigh = chkval->eidx;
	int			StopMiddle = StopHigh;
	int			difference = -1;
	bool		res = false;
	char	   *lexeme;
	WordEntry2  *entry;

	/* Loop invariant: StopLow <= val < StopHigh */
	while (StopLow < StopHigh)
	{
		StopMiddle = StopLow + (StopHigh - StopLow) / 2;
		lexeme = tsvector2_getlexeme(chkval->vec, StopMiddle, &entry);

		Assert(!entry->hasoff);
		difference = ts2_compare_string(chkval->operand + val->distance,
									 val->length,
									 lexeme,
									 entry->len,
									 false);

		if (difference == 0)
		{
			/* Check weight info & fill 'data' with positions */
			res = checkclass_str(get_lexeme_positions(lexeme, entry->len),
								 entry->npos, val, data);
			break;
		}
		else if (difference > 0)
			StopLow = StopMiddle + 1;
		else
			StopHigh = StopMiddle;
	}

	if ((!res || data) && val->prefix)
	{
		WordEntryPos *allpos = NULL;
		int			npos = 0,
					totalpos = 0;

		/*
		 * there was a failed exact search, so we should scan further to find
		 * a prefix match. We also need to do so if caller needs position info
		 */
		if (StopLow >= StopHigh)
			StopMiddle = StopHigh;

		while ((!res || data) && StopMiddle < chkval->eidx)
		{
			char	   *lexeme;
			int			cmp;
			WordEntryPos *pv;

			lexeme = tsvector2_getlexeme(chkval->vec, StopMiddle, &entry);

			Assert(!entry->hasoff);
			pv = get_lexeme_positions(lexeme, entry->len);
			cmp = ts2_compare_string(chkval->operand + val->distance,
								  val->length,
								  lexeme,
								  entry->len,
								  true);

			if (cmp != 0)
				break;

			if (data)
			{
				/*
				 * We need to join position information
				 */
				res = checkclass_str(pv, entry->npos, val, data);

				if (res)
				{
					while (npos + data->npos >= totalpos)
					{
						if (totalpos == 0)
						{
							totalpos = 256;
							allpos = palloc(sizeof(WordEntryPos) * totalpos);
						}
						else
						{
							totalpos *= 2;
							allpos = repalloc(allpos, sizeof(WordEntryPos) * totalpos);
						}
					}

					memcpy(allpos + npos, data->pos, sizeof(WordEntryPos) * data->npos);
					npos += data->npos;
				}
			}
			else
			{
				res = checkclass_str(pv, entry->npos, val, NULL);
			}

			StopMiddle++;
		}

		if (res && data)
		{
			/* Sort and make unique array of found positions */
			data->pos = allpos;
			data->npos = uniqueLongPos(allpos, npos);
			data->allocated = true;
		}
	}

	return res;
}

/*
 * Compute output position list for a tsquery operator in phrase mode.
 *
 * Merge the position lists in Ldata and Rdata as specified by "emit",
 * returning the result list into *data.  The input position lists must be
 * sorted and unique, and the output will be as well.
 *
 * data: pointer to initially-all-zeroes output struct, or NULL
 * Ldata, Rdata: input position lists
 * emit: bitmask of TSPO_XXX flags
 * Loffset: offset to be added to Ldata positions before comparing/outputting
 * Roffset: offset to be added to Rdata positions before comparing/outputting
 * max_npos: maximum possible required size of output position array
 *
 * Loffset and Roffset should not be negative, else we risk trying to output
 * negative positions, which won't fit into WordEntryPos.
 *
 * Returns true if any positions were emitted to *data; or if data is NULL,
 * returns true if any positions would have been emitted.
 */
#define TSPO_L_ONLY		0x01	/* emit positions appearing only in L */
#define TSPO_R_ONLY		0x02	/* emit positions appearing only in R */
#define TSPO_BOTH		0x04	/* emit positions appearing in both L&R */

static bool
TS_phrase_output(ExecPhraseData *data,
				 ExecPhraseData *Ldata,
				 ExecPhraseData *Rdata,
				 int emit,
				 int Loffset,
				 int Roffset,
				 int max_npos)
{
	int			Lindex,
				Rindex;

	/* Loop until both inputs are exhausted */
	Lindex = Rindex = 0;
	while (Lindex < Ldata->npos || Rindex < Rdata->npos)
	{
		int			Lpos,
					Rpos;
		int			output_pos = 0;

		/*
		 * Fetch current values to compare.  WEP_GETPOS() is needed because
		 * ExecPhraseData->data can point to a tsvector2's WordEntryPosVector.
		 */
		if (Lindex < Ldata->npos)
			Lpos = WEP_GETPOS(Ldata->pos[Lindex]) + Loffset;
		else
		{
			/* L array exhausted, so we're done if R_ONLY isn't set */
			if (!(emit & TSPO_R_ONLY))
				break;
			Lpos = INT_MAX;
		}
		if (Rindex < Rdata->npos)
			Rpos = WEP_GETPOS(Rdata->pos[Rindex]) + Roffset;
		else
		{
			/* R array exhausted, so we're done if L_ONLY isn't set */
			if (!(emit & TSPO_L_ONLY))
				break;
			Rpos = INT_MAX;
		}

		/* Merge-join the two input lists */
		if (Lpos < Rpos)
		{
			/* Lpos is not matched in Rdata, should we output it? */
			if (emit & TSPO_L_ONLY)
				output_pos = Lpos;
			Lindex++;
		}
		else if (Lpos == Rpos)
		{
			/* Lpos and Rpos match ... should we output it? */
			if (emit & TSPO_BOTH)
				output_pos = Rpos;
			Lindex++;
			Rindex++;
		}
		else					/* Lpos > Rpos */
		{
			/* Rpos is not matched in Ldata, should we output it? */
			if (emit & TSPO_R_ONLY)
				output_pos = Rpos;
			Rindex++;
		}

		if (output_pos > 0)
		{
			if (data)
			{
				/* Store position, first allocating output array if needed */
				if (data->pos == NULL)
				{
					data->pos = (WordEntryPos *)
						palloc(max_npos * sizeof(WordEntryPos));
					data->allocated = true;
				}
				data->pos[data->npos++] = output_pos;
			}
			else
			{
				/*
				 * Exact positions not needed, so return true as soon as we
				 * know there is at least one.
				 */
				return true;
			}
		}
	}

	if (data && data->npos > 0)
	{
		/* Let's assert we didn't overrun the array */
		Assert(data->npos <= max_npos);
		return true;
	}
	return false;
}

/*
 * Execute tsquery at or below an OP_PHRASE operator.
 *
 * This handles tsquery execution at recursion levels where we need to care
 * about match locations.
 *
 * In addition to the same arguments used for TS_execute, the caller may pass
 * a preinitialized-to-zeroes ExecPhraseData struct, to be filled with lexeme
 * match position info on success.  data == NULL if no position data need be
 * returned.  (In practice, outside callers pass NULL, and only the internal
 * recursion cases pass a data pointer.)
 * Note: the function assumes data != NULL for operators other than OP_PHRASE.
 * This is OK because an outside call always starts from an OP_PHRASE node.
 *
 * The detailed semantics of the match data, given that the function returned
 * "true" (successful match, or possible match), are:
 *
 * npos > 0, negate = false:
 *	 query is matched at specified position(s) (and only those positions)
 * npos > 0, negate = true:
 *	 query is matched at all positions *except* specified position(s)
 * npos = 0, negate = false:
 *	 query is possibly matched, matching position(s) are unknown
 *	 (this should only be returned when TS_EXEC_PHRASE_NO_POS flag is set)
 * npos = 0, negate = true:
 *	 query is matched at all positions
 *
 * Successful matches also return a "width" value which is the match width in
 * lexemes, less one.  Hence, "width" is zero for simple one-lexeme matches,
 * and is the sum of the phrase operator distances for phrase matches.  Note
 * that when width > 0, the listed positions represent the ends of matches not
 * the starts.  (This unintuitive rule is needed to avoid possibly generating
 * negative positions, which wouldn't fit into the WordEntryPos arrays.)
 *
 * When the function returns "false" (no match), it must return npos = 0,
 * negate = false (which is the state initialized by the caller); but the
 * "width" output in such cases is undefined.
 */
static bool
TS_phrase_execute(QueryItem *curitem, void *arg, uint32 flags,
				  TSExecuteCallback chkcond,
				  ExecPhraseData *data)
{
	ExecPhraseData Ldata,
				Rdata;
	bool		lmatch,
				rmatch;
	int			Loffset,
				Roffset,
				maxwidth;

	/* since this function recurses, it could be driven to stack overflow */
	check_stack_depth();

	if (curitem->type == QI_VAL)
		return chkcond(arg, (QueryOperand *) curitem, data);

	switch (curitem->qoperator.oper)
	{
		case OP_NOT:

			/*
			 * Because a "true" result with no specific positions is taken as
			 * uncertain, we need no special care here for !TS_EXEC_CALC_NOT.
			 * If it's a false positive, the right things happen anyway.
			 *
			 * Also, we need not touch data->width, since a NOT operation does
			 * not change the match width.
			 */
			if (TS_phrase_execute(curitem + 1, arg, flags, chkcond, data))
			{
				if (data->npos > 0)
				{
					/* we have some positions, invert negate flag */
					data->negate = !data->negate;
					return true;
				}
				else if (data->negate)
				{
					/* change "match everywhere" to "match nowhere" */
					data->negate = false;
					return false;
				}
				/* match positions are, and remain, uncertain */
				return true;
			}
			else
			{
				/* change "match nowhere" to "match everywhere" */
				Assert(data->npos == 0 && !data->negate);
				data->negate = true;
				return true;
			}

		case OP_PHRASE:
		case OP_AND:
			memset(&Ldata, 0, sizeof(Ldata));
			memset(&Rdata, 0, sizeof(Rdata));

			if (!TS_phrase_execute(curitem + curitem->qoperator.left,
								   arg, flags, chkcond, &Ldata))
				return false;

			if (!TS_phrase_execute(curitem + 1,
								   arg, flags, chkcond, &Rdata))
				return false;

			/*
			 * If either operand has no position information, then we can't
			 * return position data, only a "possible match" result. "Possible
			 * match" answers are only wanted when TS_EXEC_PHRASE_NO_POS flag
			 * is set, otherwise return false.
			 */
			if ((Ldata.npos == 0 && !Ldata.negate) ||
				(Rdata.npos == 0 && !Rdata.negate))
				return (flags & TS_EXEC_PHRASE_NO_POS) ? true : false;

			if (curitem->qoperator.oper == OP_PHRASE)
			{
				/*
				 * Compute Loffset and Roffset suitable for phrase match, and
				 * compute overall width of whole phrase match.
				 */
				Loffset = curitem->qoperator.distance + Rdata.width;
				Roffset = 0;
				if (data)
					data->width = curitem->qoperator.distance +
						Ldata.width + Rdata.width;
			}
			else
			{
				/*
				 * For OP_AND, set output width and alignment like OP_OR (see
				 * comment below)
				 */
				maxwidth = Max(Ldata.width, Rdata.width);
				Loffset = maxwidth - Ldata.width;
				Roffset = maxwidth - Rdata.width;
				if (data)
					data->width = maxwidth;
			}

			if (Ldata.negate && Rdata.negate)
			{
				/* !L & !R: treat as !(L | R) */
				(void) TS_phrase_output(data, &Ldata, &Rdata,
										TSPO_BOTH | TSPO_L_ONLY | TSPO_R_ONLY,
										Loffset, Roffset,
										Ldata.npos + Rdata.npos);
				if (data)
					data->negate = true;
				return true;
			}
			else if (Ldata.negate)
			{
				/* !L & R */
				return TS_phrase_output(data, &Ldata, &Rdata,
										TSPO_R_ONLY,
										Loffset, Roffset,
										Rdata.npos);
			}
			else if (Rdata.negate)
			{
				/* L & !R */
				return TS_phrase_output(data, &Ldata, &Rdata,
										TSPO_L_ONLY,
										Loffset, Roffset,
										Ldata.npos);
			}
			else
			{
				/* straight AND */
				return TS_phrase_output(data, &Ldata, &Rdata,
										TSPO_BOTH,
										Loffset, Roffset,
										Min(Ldata.npos, Rdata.npos));
			}

		case OP_OR:
			memset(&Ldata, 0, sizeof(Ldata));
			memset(&Rdata, 0, sizeof(Rdata));

			lmatch = TS_phrase_execute(curitem + curitem->qoperator.left,
									   arg, flags, chkcond, &Ldata);
			rmatch = TS_phrase_execute(curitem + 1,
									   arg, flags, chkcond, &Rdata);

			if (!lmatch && !rmatch)
				return false;

			/*
			 * If a valid operand has no position information, then we can't
			 * return position data, only a "possible match" result. "Possible
			 * match" answers are only wanted when TS_EXEC_PHRASE_NO_POS flag
			 * is set, otherwise return false.
			 */
			if ((lmatch && Ldata.npos == 0 && !Ldata.negate) ||
				(rmatch && Rdata.npos == 0 && !Rdata.negate))
				return (flags & TS_EXEC_PHRASE_NO_POS) ? true : false;

			/*
			 * Cope with undefined output width from failed submatch.  (This
			 * takes less code than trying to ensure that all failure returns
			 * set data->width to zero.)
			 */
			if (!lmatch)
				Ldata.width = 0;
			if (!rmatch)
				Rdata.width = 0;

			/*
			 * For OP_AND and OP_OR, report the width of the wider of the two
			 * inputs, and align the narrower input's positions to the right
			 * end of that width.  This rule deals at least somewhat
			 * reasonably with cases like "x <-> (y | z <-> q)".
			 */
			maxwidth = Max(Ldata.width, Rdata.width);
			Loffset = maxwidth - Ldata.width;
			Roffset = maxwidth - Rdata.width;
			data->width = maxwidth;

			if (Ldata.negate && Rdata.negate)
			{
				/* !L | !R: treat as !(L & R) */
				(void) TS_phrase_output(data, &Ldata, &Rdata,
										TSPO_BOTH,
										Loffset, Roffset,
										Min(Ldata.npos, Rdata.npos));
				data->negate = true;
				return true;
			}
			else if (Ldata.negate)
			{
				/* !L | R: treat as !(L & !R) */
				(void) TS_phrase_output(data, &Ldata, &Rdata,
										TSPO_L_ONLY,
										Loffset, Roffset,
										Ldata.npos);
				data->negate = true;
				return true;
			}
			else if (Rdata.negate)
			{
				/* L | !R: treat as !(!L & R) */
				(void) TS_phrase_output(data, &Ldata, &Rdata,
										TSPO_R_ONLY,
										Loffset, Roffset,
										Rdata.npos);
				data->negate = true;
				return true;
			}
			else
			{
				/* straight OR */
				return TS_phrase_output(data, &Ldata, &Rdata,
										TSPO_BOTH | TSPO_L_ONLY | TSPO_R_ONLY,
										Loffset, Roffset,
										Ldata.npos + Rdata.npos);
			}

		default:
			elog(ERROR, "unrecognized operator: %d", curitem->qoperator.oper);
	}

	/* not reachable, but keep compiler quiet */
	return false;
}


/*
 * Evaluate tsquery boolean expression.
 *
 * curitem: current tsquery item (initially, the first one)
 * arg: opaque value to pass through to callback function
 * flags: bitmask of flag bits shown in ts_utils.h
 * chkcond: callback function to check whether a primitive value is present
 *
 * The logic here deals only with operators above any phrase operator, for
 * which we do not need to worry about lexeme positions.  As soon as we hit an
 * OP_PHRASE operator, we pass it off to TS_phrase_execute which does worry.
 */
bool
TS_execute(QueryItem *curitem, void *arg, uint32 flags,
		   TSExecuteCallback chkcond)
{
	/* since this function recurses, it could be driven to stack overflow */
	check_stack_depth();

	if (curitem->type == QI_VAL)
		return chkcond(arg, (QueryOperand *) curitem,
					   NULL /* we don't need position info */ );

	switch (curitem->qoperator.oper)
	{
		case OP_NOT:
			if (flags & TS_EXEC_CALC_NOT)
				return !TS_execute(curitem + 1, arg, flags, chkcond);
			else
				return true;

		case OP_AND:
			if (TS_execute(curitem + curitem->qoperator.left, arg, flags, chkcond))
				return TS_execute(curitem + 1, arg, flags, chkcond);
			else
				return false;

		case OP_OR:
			if (TS_execute(curitem + curitem->qoperator.left, arg, flags, chkcond))
				return true;
			else
				return TS_execute(curitem + 1, arg, flags, chkcond);

		case OP_PHRASE:
			return TS_phrase_execute(curitem, arg, flags, chkcond, NULL);

		default:
			elog(ERROR, "unrecognized operator: %d", curitem->qoperator.oper);
	}

	/* not reachable, but keep compiler quiet */
	return false;
}

/*
 * Detect whether a tsquery boolean expression requires any positive matches
 * to values shown in the tsquery.
 *
 * This is needed to know whether a GIN index search requires full index scan.
 * For example, 'x & !y' requires a match of x, so it's sufficient to scan
 * entries for x; but 'x | !y' could match rows containing neither x nor y.
 */
bool
tsquery_requires_match(QueryItem *curitem)
{
	/* since this function recurses, it could be driven to stack overflow */
	check_stack_depth();

	if (curitem->type == QI_VAL)
		return true;

	switch (curitem->qoperator.oper)
	{
		case OP_NOT:

			/*
			 * Assume there are no required matches underneath a NOT.  For
			 * some cases with nested NOTs, we could prove there's a required
			 * match, but it seems unlikely to be worth the trouble.
			 */
			return false;

		case OP_PHRASE:

			/*
			 * Treat OP_PHRASE as OP_AND here
			 */
		case OP_AND:
			/* If either side requires a match, we're good */
			if (tsquery_requires_match(curitem + curitem->qoperator.left))
				return true;
			else
				return tsquery_requires_match(curitem + 1);

		case OP_OR:
			/* Both sides must require a match */
			if (tsquery_requires_match(curitem + curitem->qoperator.left))
				return tsquery_requires_match(curitem + 1);
			else
				return false;

		default:
			elog(ERROR, "unrecognized operator: %d", curitem->qoperator.oper);
	}

	/* not reachable, but keep compiler quiet */
	return false;
}

/*
 * boolean operations
 */
PG_FUNCTION_INFO_V1(tsquery_match_tsvector2);
Datum
tsquery_match_tsvector2(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(DirectFunctionCall2(tsvector2_match_tsquery,
										PG_GETARG_DATUM(1),
										PG_GETARG_DATUM(0)));
}

Datum
tsvector2_match_tsquery(PG_FUNCTION_ARGS)
{
	TSVector2	val = PG_GETARG_TSVECTOR2(0);
	TSQuery		query = PG_GETARG_TSQUERY(1);
	CHKVAL		chkval;
	bool		result;

	/* empty query matches nothing */
	if (!query->size)
	{
		PG_FREE_IF_COPY(val, 0);
		PG_FREE_IF_COPY(query, 1);
		PG_RETURN_BOOL(false);
	}

	chkval.bidx = 0;
	chkval.eidx = val->size;
	chkval.vec = val;
	chkval.operand = GETOPERAND(query);
	result = TS_execute(GETQUERY(query),
						&chkval,
						TS_EXEC_CALC_NOT,
						checkcondition_str);

	PG_FREE_IF_COPY(val, 0);
	PG_FREE_IF_COPY(query, 1);
	PG_RETURN_BOOL(result);
}

/*
 * ts_stat statistic function support
 */


/*
 * Returns the number of positions in value 'wptr' within tsvector2 'txt',
 * that have a weight equal to one of the weights in 'weight' bitmask.
 */
static int
check_weight(char *lexeme, WordEntry2 *wptr, int8 weight)
{
	int			len;
	int			num = 0;
	WordEntryPos *ptr;

	Assert(!wptr->hasoff);
	len = wptr->len;
	ptr = get_lexeme_positions(lexeme, len);
	while (len--)
	{
		if (weight & (1 << WEP_GETWEIGHT(*ptr)))
			num++;
		ptr++;
	}
	return num;
}

#define compareStatWord(a,e,t)							\
	(ts2_compare_string((a)->lexeme, (a)->lenlexeme,		\
					t, (e)->len, false))

static void
insertStatEntry(MemoryContext persistentContext, TSVectorStat *stat, TSVector2 txt, uint32 off)
{
	WordEntry2  *we;
	StatEntry  *node = stat->root,
			   *pnode = NULL;
	int			n,
				res = 0;
	uint32		depth = 1;
	char	   *lexeme;

	lexeme = tsvector2_getlexeme(txt, off, &we);

	Assert(!we->hasoff);
	if (stat->weight == 0)
		n = (we->npos) ? we->npos : 1;
	else
		n = (we->npos) ? check_weight(lexeme, we, stat->weight) : 0;

	if (n == 0)
		return;					/* nothing to insert */

	while (node)
	{
		res = compareStatWord(node, we, lexeme);

		if (res == 0)
		{
			break;
		}
		else
		{
			pnode = node;
			node = (res < 0) ? node->left : node->right;
		}
		depth++;
	}

	if (depth > stat->maxdepth)
		stat->maxdepth = depth;

	if (node == NULL)
	{
		node = MemoryContextAlloc(persistentContext, STATENTRYHDRSZ + we->len);
		node->left = node->right = NULL;
		node->ndoc = 1;
		node->nentry = n;
		node->lenlexeme = we->len;
		memcpy(node->lexeme, lexeme, node->lenlexeme);

		if (pnode == NULL)
		{
			stat->root = node;
		}
		else
		{
			if (res < 0)
				pnode->left = node;
			else
				pnode->right = node;
		}

	}
	else
	{
		node->ndoc++;
		node->nentry += n;
	}
}

static void
chooseNextStatEntry(MemoryContext persistentContext, TSVectorStat *stat, TSVector2 txt,
					uint32 low, uint32 high, uint32 offset)
{
	uint32		pos;
	uint32		middle = (low + high) >> 1,
				count = txt->size; 

	pos = (low + middle) >> 1;
	if (low != middle && pos >= offset && pos - offset < count)
		insertStatEntry(persistentContext, stat, txt, pos - offset);
	pos = (high + middle + 1) >> 1;
	if (middle + 1 != high && pos >= offset && pos - offset < count)
		insertStatEntry(persistentContext, stat, txt, pos - offset);

	if (low != middle)
		chooseNextStatEntry(persistentContext, stat, txt, low, middle, offset);
	if (high != middle + 1)
		chooseNextStatEntry(persistentContext, stat, txt, middle + 1, high, offset);
}

/*
 * This is written like a custom aggregate function, because the
 * original plan was to do just that. Unfortunately, an aggregate function
 * can't return a set, so that plan was abandoned. If that limitation is
 * lifted in the future, ts_stat could be a real aggregate function so that
 * you could use it like this:
 *
 *	 SELECT ts_stat(vector_column) FROM vector_table;
 *
 *	where vector_column is a tsvector2-type column in vector_table.
 */

static TSVectorStat *
ts_accum(MemoryContext persistentContext, TSVectorStat *stat, Datum data)
{
	TSVector2	txt = DatumGetTSVector2(data);
	uint32		i,
				nbit = 0,
				offset,
				count = txt->size;

	if (stat == NULL)
	{							/* Init in first */
		stat = MemoryContextAllocZero(persistentContext, sizeof(TSVectorStat));
		stat->maxdepth = 1;
	}

	/* simple check of correctness */
	if (txt == NULL || count == 0)
	{
		if (txt && txt != (TSVector2) DatumGetPointer(data))
			pfree(txt);
		return stat;
	}

	i = count - 1;
	for (; i > 0; i >>= 1)
		nbit++;

	nbit = 1 << nbit;
	offset = (nbit - count) / 2;

	insertStatEntry(persistentContext, stat, txt, (nbit >> 1) - offset);
	chooseNextStatEntry(persistentContext, stat, txt, 0, nbit, offset);

	return stat;
}

static void
ts_setup_firstcall(FunctionCallInfo fcinfo, FuncCallContext *funcctx,
				   TSVectorStat *stat)
{
	TupleDesc	tupdesc;
	MemoryContext oldcontext;
	StatEntry  *node;

	funcctx->user_fctx = (void *) stat;

	oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

	stat->stack = palloc0(sizeof(StatEntry *) * (stat->maxdepth + 1));
	stat->stackpos = 0;

	node = stat->root;
	/* find leftmost value */
	if (node == NULL)
		stat->stack[stat->stackpos] = NULL;
	else
		for (;;)
		{
			stat->stack[stat->stackpos] = node;
			if (node->left)
			{
				stat->stackpos++;
				node = node->left;
			}
			else
				break;
		}
	Assert(stat->stackpos <= stat->maxdepth);

	tupdesc = CreateTemplateTupleDesc(3, false);
	TupleDescInitEntry(tupdesc, (AttrNumber) 1, "word",
					   TEXTOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 2, "ndoc",
					   INT4OID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 3, "nentry",
					   INT4OID, -1, 0);
	funcctx->tuple_desc = BlessTupleDesc(tupdesc);
	funcctx->attinmeta = TupleDescGetAttInMetadata(tupdesc);

	MemoryContextSwitchTo(oldcontext);
}

static StatEntry *
walkStatEntryTree(TSVectorStat *stat)
{
	StatEntry  *node = stat->stack[stat->stackpos];

	if (node == NULL)
		return NULL;

	if (node->ndoc != 0)
	{
		/* return entry itself: we already was at left sublink */
		return node;
	}
	else if (node->right && node->right != stat->stack[stat->stackpos + 1])
	{
		/* go on right sublink */
		stat->stackpos++;
		node = node->right;

		/* find most-left value */
		for (;;)
		{
			stat->stack[stat->stackpos] = node;
			if (node->left)
			{
				stat->stackpos++;
				node = node->left;
			}
			else
				break;
		}
		Assert(stat->stackpos <= stat->maxdepth);
	}
	else
	{
		/* we already return all left subtree, itself and  right subtree */
		if (stat->stackpos == 0)
			return NULL;

		stat->stackpos--;
		return walkStatEntryTree(stat);
	}

	return node;
}

static Datum
ts_process_call(FuncCallContext *funcctx)
{
	TSVectorStat *st;
	StatEntry  *entry;

	st = (TSVectorStat *) funcctx->user_fctx;

	entry = walkStatEntryTree(st);

	if (entry != NULL)
	{
		Datum		result;
		char	   *values[3];
		char		ndoc[16];
		char		nentry[16];
		HeapTuple	tuple;

		values[0] = palloc(entry->lenlexeme + 1);
		memcpy(values[0], entry->lexeme, entry->lenlexeme);
		(values[0])[entry->lenlexeme] = '\0';
		sprintf(ndoc, "%d", entry->ndoc);
		values[1] = ndoc;
		sprintf(nentry, "%d", entry->nentry);
		values[2] = nentry;

		tuple = BuildTupleFromCStrings(funcctx->attinmeta, values);
		result = HeapTupleGetDatum(tuple);

		pfree(values[0]);

		/* mark entry as already visited */
		entry->ndoc = 0;

		return result;
	}

	return (Datum) 0;
}

static TSVectorStat *
ts_stat_sql(MemoryContext persistentContext, text *txt, text *ws)
{
	char	   *query = text_to_cstring(txt);
	TSVectorStat *stat;
	bool		isnull;
	Portal		portal;
	SPIPlanPtr	plan;
	Oid			oid;

	if ((plan = SPI_prepare(query, 0, NULL)) == NULL)
		/* internal error */
		elog(ERROR, "SPI_prepare(\"%s\") failed", query);

	if ((portal = SPI_cursor_open(NULL, plan, NULL, NULL, true)) == NULL)
		/* internal error */
		elog(ERROR, "SPI_cursor_open(\"%s\") failed", query);

	SPI_cursor_fetch(portal, true, 100);

	oid = TypenameGetTypid("tsvector2");

	if (SPI_tuptable == NULL ||
		SPI_tuptable->tupdesc->natts != 1 ||
		!IsBinaryCoercible(SPI_gettypeid(SPI_tuptable->tupdesc, 1),
						   oid))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("ts_stat query must return one tsvector2 column")));

	stat = MemoryContextAllocZero(persistentContext, sizeof(TSVectorStat));
	stat->maxdepth = 1;

	if (ws)
	{
		char	   *buf;

		buf = VARDATA_ANY(ws);
		while (buf - VARDATA_ANY(ws) < VARSIZE_ANY_EXHDR(ws))
		{
			if (pg_mblen(buf) == 1)
			{
				switch (*buf)
				{
					case 'A':
					case 'a':
						stat->weight |= 1 << 3;
						break;
					case 'B':
					case 'b':
						stat->weight |= 1 << 2;
						break;
					case 'C':
					case 'c':
						stat->weight |= 1 << 1;
						break;
					case 'D':
					case 'd':
						stat->weight |= 1;
						break;
					default:
						stat->weight |= 0;
				}
			}
			buf += pg_mblen(buf);
		}
	}

	while (SPI_processed > 0)
	{
		uint64		i;

		for (i = 0; i < SPI_processed; i++)
		{
			Datum		data = SPI_getbinval(SPI_tuptable->vals[i], SPI_tuptable->tupdesc, 1, &isnull);

			if (!isnull)
				stat = ts_accum(persistentContext, stat, data);
		}

		SPI_freetuptable(SPI_tuptable);
		SPI_cursor_fetch(portal, true, 100);
	}

	SPI_freetuptable(SPI_tuptable);
	SPI_cursor_close(portal);
	SPI_freeplan(plan);
	pfree(query);

	return stat;
}

PG_FUNCTION_INFO_V1(tsvector2_stat1);
Datum
tsvector2_stat1(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	Datum		result;

	if (SRF_IS_FIRSTCALL())
	{
		TSVectorStat *stat;
		text	   *txt = PG_GETARG_TEXT_PP(0);

		funcctx = SRF_FIRSTCALL_INIT();
		SPI_connect();
		stat = ts_stat_sql(funcctx->multi_call_memory_ctx, txt, NULL);
		PG_FREE_IF_COPY(txt, 0);
		ts_setup_firstcall(fcinfo, funcctx, stat);
		SPI_finish();
	}

	funcctx = SRF_PERCALL_SETUP();
	if ((result = ts_process_call(funcctx)) != (Datum) 0)
		SRF_RETURN_NEXT(funcctx, result);
	SRF_RETURN_DONE(funcctx);
}

PG_FUNCTION_INFO_V1(tsvector2_stat2);
Datum
tsvector2_stat2(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	Datum		result;

	if (SRF_IS_FIRSTCALL())
	{
		TSVectorStat *stat;
		text	   *txt = PG_GETARG_TEXT_PP(0);
		text	   *ws = PG_GETARG_TEXT_PP(1);

		funcctx = SRF_FIRSTCALL_INIT();
		SPI_connect();
		stat = ts_stat_sql(funcctx->multi_call_memory_ctx, txt, ws);
		PG_FREE_IF_COPY(txt, 0);
		PG_FREE_IF_COPY(ws, 1);
		ts_setup_firstcall(fcinfo, funcctx, stat);
		SPI_finish();
	}

	funcctx = SRF_PERCALL_SETUP();
	if ((result = ts_process_call(funcctx)) != (Datum) 0)
		SRF_RETURN_NEXT(funcctx, result);
	SRF_RETURN_DONE(funcctx);
}

/*
 * Triggers for automatic update of a tsvector2 column from text column(s)
 *
 * Trigger arguments are either
 *		name of tsvector2 col, name of tsconfig to use, name(s) of text col(s)
 *		name of tsvector2 col, name of regconfig col, name(s) of text col(s)
 * ie, tsconfig can either be specified by name, or indirectly as the
 * contents of a regconfig field in the row.  If the name is used, it must
 * be explicitly schema-qualified.
 */
static Datum
tsvector2_update_trigger(PG_FUNCTION_ARGS, bool config_column)
{
	TriggerData *trigdata;
	Trigger    *trigger;
	Relation	rel;
	HeapTuple	rettuple = NULL;
	int			tsvector_attr_num,
				i;
	ParsedText	prs;
	Datum		datum;
	bool		isnull;
	text	   *txt;
	Oid			cfgId;

	/* Check call context */
	if (!CALLED_AS_TRIGGER(fcinfo)) /* internal error */
		elog(ERROR, "tsvector2_update_trigger: not fired by trigger manager");

	trigdata = (TriggerData *) fcinfo->context;
	if (!TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
		elog(ERROR, "tsvector2_update_trigger: must be fired for row");
	if (!TRIGGER_FIRED_BEFORE(trigdata->tg_event))
		elog(ERROR, "tsvector2_update_trigger: must be fired BEFORE event");

	if (TRIGGER_FIRED_BY_INSERT(trigdata->tg_event))
		rettuple = trigdata->tg_trigtuple;
	else if (TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
		rettuple = trigdata->tg_newtuple;
	else
		elog(ERROR, "tsvector2_update_trigger: must be fired for INSERT or UPDATE");

	trigger = trigdata->tg_trigger;
	rel = trigdata->tg_relation;

	if (trigger->tgnargs < 3)
		elog(ERROR, "tsvector2_update_trigger: arguments must be tsvector_field, ts_config, text_field1, ...)");

	/* Find the target tsvector2 column */
	tsvector_attr_num = SPI_fnumber(rel->rd_att, trigger->tgargs[0]);
	if (tsvector_attr_num == SPI_ERROR_NOATTRIBUTE)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("tsvector2 column \"%s\" does not exist",
						trigger->tgargs[0])));
	/* This will effectively reject system columns, so no separate test: */
	if (strcmp(SPI_gettype(rel->rd_att, tsvector_attr_num), "tsvector2") != 0)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("column \"%s\" is not of tsvector2 type",
						trigger->tgargs[0])));

	/* Find the configuration to use */
	if (config_column)
	{
		int			config_attr_num;

		config_attr_num = SPI_fnumber(rel->rd_att, trigger->tgargs[1]);
		if (config_attr_num == SPI_ERROR_NOATTRIBUTE)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_COLUMN),
					 errmsg("configuration column \"%s\" does not exist",
							trigger->tgargs[1])));
		if (!IsBinaryCoercible(SPI_gettypeid(rel->rd_att, config_attr_num),
							   REGCONFIGOID))
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("column \"%s\" is not of regconfig type",
							trigger->tgargs[1])));

		datum = SPI_getbinval(rettuple, rel->rd_att, config_attr_num, &isnull);
		if (isnull)
			ereport(ERROR,
					(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
					 errmsg("configuration column \"%s\" must not be null",
							trigger->tgargs[1])));
		cfgId = DatumGetObjectId(datum);
	}
	else
	{
		List	   *names;

		names = stringToQualifiedNameList(trigger->tgargs[1]);
		/* require a schema so that results are not search path dependent */
		if (list_length(names) < 2)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("text search configuration name \"%s\" must be schema-qualified",
							trigger->tgargs[1])));
		cfgId = get_ts_config_oid(names, false);
	}

	/* initialize parse state */
	prs.lenwords = 32;
	prs.curwords = 0;
	prs.pos = 0;
	prs.words = (ParsedWord *) palloc(sizeof(ParsedWord) * prs.lenwords);

	/* find all words in indexable column(s) */
	for (i = 2; i < trigger->tgnargs; i++)
	{
		int			numattr;

		numattr = SPI_fnumber(rel->rd_att, trigger->tgargs[i]);
		if (numattr == SPI_ERROR_NOATTRIBUTE)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_COLUMN),
					 errmsg("column \"%s\" does not exist",
							trigger->tgargs[i])));
		if (!IsBinaryCoercible(SPI_gettypeid(rel->rd_att, numattr), TEXTOID))
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("column \"%s\" is not of a character type",
							trigger->tgargs[i])));

		datum = SPI_getbinval(rettuple, rel->rd_att, numattr, &isnull);
		if (isnull)
			continue;

		txt = DatumGetTextPP(datum);

		parsetext(cfgId, &prs, VARDATA_ANY(txt), VARSIZE_ANY_EXHDR(txt));

		if (txt != (text *) DatumGetPointer(datum))
			pfree(txt);
	}

#if PG_VERSION_NUM >= 100000
	/* make tsvector2 value */
	if (prs.curwords)
	{
		datum = PointerGetDatum(make_tsvector2(&prs));
		isnull = false;
		rettuple = heap_modify_tuple_by_cols(rettuple, rel->rd_att,
											 1, &tsvector_attr_num,
											 &datum, &isnull);
		pfree(DatumGetPointer(datum));
	}
	else
	{
		TSVector2	out = palloc(tsvector2_calcsize(0, 0));

		SET_VARSIZE(out, tsvector2_calcsize(0, 0));
		out->size = 0;
		datum = PointerGetDatum(out);
		isnull = false;
		rettuple = heap_modify_tuple_by_cols(rettuple, rel->rd_att,
											 1, &tsvector_attr_num,
											 &datum, &isnull);
		pfree(prs.words);
	}
#else
	/* make tsvector2 value */
	if (prs.curwords)
	{
		datum = PointerGetDatum(make_tsvector2(&prs));
		rettuple = SPI_modifytuple(rel, rettuple, 1, &tsvector_attr_num,
								   &datum, NULL);
		pfree(DatumGetPointer(datum));
	}
	else
	{
		TSVector2	out = palloc(CALCDATASIZE(0, 0));

		SET_VARSIZE(out, tsvector2_calcsize(0, 0));
		out->size = 0;
		datum = PointerGetDatum(out);
		rettuple = SPI_modifytuple(rel, rettuple, 1, &tsvector_attr_num,
								   &datum, NULL);
		pfree(prs.words);
	}

	if (rettuple == NULL)		/* internal error */
		elog(ERROR, "tsvector_update_trigger: %d returned by SPI_modifytuple",
			 SPI_result);
#endif

	return PointerGetDatum(rettuple);
}

PG_FUNCTION_INFO_V1(tsvector2_update_trigger_byid);
Datum
tsvector2_update_trigger_byid(PG_FUNCTION_ARGS)
{
	return tsvector2_update_trigger(fcinfo, false);
}

PG_FUNCTION_INFO_V1(tsvector2_update_trigger_bycolumn);
Datum
tsvector2_update_trigger_bycolumn(PG_FUNCTION_ARGS)
{
	return tsvector2_update_trigger(fcinfo, true);
}
