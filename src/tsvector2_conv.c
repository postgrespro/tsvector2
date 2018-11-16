/*-------------------------------------------------------------------------
 *
 * tsvector2_json.c
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 2018, PostgresPro
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "tsearch/ts_cache.h"
#include "utils/builtins.h"
#include "utils/jsonapi.h"
#include "tsearch/ts_utils.h"

#include "tsvector2.h"

typedef struct TSVectorBuildState
{
	ParsedText *prs;
	Oid			cfgId;
} TSVectorBuildState;

static void add_to_tsvector2(void *_state, char *elem_value, int elem_len);

/*
 * Worker function for jsonb(_string)_to_tsvector2(_byid)
 */
static TSVector2
jsonb_to_tsvector2_worker(Oid cfgId, Jsonb *jb, uint32 flags)
{
	TSVectorBuildState state;
	ParsedText	prs;

	prs.words = NULL;
	prs.curwords = 0;
	state.prs = &prs;
	state.cfgId = cfgId;

#if PG_VERSION_NUM >= 110000
	iterate_jsonb_values(jb, flags, &state, add_to_tsvector2);
#else
	iterate_jsonb_string_values(jb, &state, add_to_tsvector2);
#endif

	return make_tsvector2(&prs);
}

PG_FUNCTION_INFO_V1(jsonb_string_to_tsvector2_byid);
Datum
jsonb_string_to_tsvector2_byid(PG_FUNCTION_ARGS)
{
	Oid			cfgId = PG_GETARG_OID(0);
	Jsonb	   *jb = PG_GETARG_JSONB_P(1);
	TSVector2	result;

	result = jsonb_to_tsvector2_worker(cfgId, jb, jtiString);
	PG_FREE_IF_COPY(jb, 1);

	PG_RETURN_TSVECTOR2(result);
}

PG_FUNCTION_INFO_V1(jsonb_string_to_tsvector2);
Datum
jsonb_string_to_tsvector2(PG_FUNCTION_ARGS)
{
	Jsonb	   *jb = PG_GETARG_JSONB_P(0);
	Oid			cfgId;
	TSVector2	result;

	cfgId = getTSCurrentConfig(true);
	result = jsonb_to_tsvector2_worker(cfgId, jb, jtiString);
	PG_FREE_IF_COPY(jb, 0);

	PG_RETURN_TSVECTOR2(result);
}

PG_FUNCTION_INFO_V1(jsonb_to_tsvector2_byid);
Datum
jsonb_to_tsvector2_byid(PG_FUNCTION_ARGS)
{
	Oid			cfgId = PG_GETARG_OID(0);
	Jsonb	   *jb = PG_GETARG_JSONB_P(1);
	Jsonb	   *jbFlags = PG_GETARG_JSONB_P(2);
	TSVector2	result;
	uint32		flags = 0;

#if PG_VERSION_NUM >= 110000
	flags = parse_jsonb_index_flags(jbFlags);
#endif

	result = jsonb_to_tsvector2_worker(cfgId, jb, flags);
	PG_FREE_IF_COPY(jb, 1);
	PG_FREE_IF_COPY(jbFlags, 2);

	PG_RETURN_TSVECTOR2(result);
}

PG_FUNCTION_INFO_V1(jsonb_to_tsvector2);
Datum
jsonb_to_tsvector2(PG_FUNCTION_ARGS)
{
	Jsonb	   *jb = PG_GETARG_JSONB_P(0);
	Jsonb	   *jbFlags = PG_GETARG_JSONB_P(1);
	Oid			cfgId;
	TSVector2	result;
	uint32		flags = 0;

#if PG_VERSION_NUM >= 110000
	flags = parse_jsonb_index_flags(jbFlags);
#endif

	cfgId = getTSCurrentConfig(true);
	result = jsonb_to_tsvector2_worker(cfgId, jb, flags);
	PG_FREE_IF_COPY(jb, 0);
	PG_FREE_IF_COPY(jbFlags, 1);

	PG_RETURN_TSVECTOR2(result);
}

/*
 * Worker function for json(_string)_to_tsvector2(_byid)
 */
static TSVector2
json_to_tsvector2_worker(Oid cfgId, text *json, uint32 flags)
{
	TSVectorBuildState state;
	ParsedText	prs;

	prs.words = NULL;
	prs.curwords = 0;
	state.prs = &prs;
	state.cfgId = cfgId;

#if PG_VERSION_NUM >= 110000
	iterate_json_values(json, flags, &state, add_to_tsvector2);
#else
	iterate_json_string_values(json, &state, add_to_tsvector2);
#endif

	return make_tsvector2(&prs);
}

PG_FUNCTION_INFO_V1(json_string_to_tsvector2_byid);
Datum
json_string_to_tsvector2_byid(PG_FUNCTION_ARGS)
{
	Oid			cfgId = PG_GETARG_OID(0);
	text	   *json = PG_GETARG_TEXT_P(1);
	TSVector2	result;

	result = json_to_tsvector2_worker(cfgId, json, jtiString);
	PG_FREE_IF_COPY(json, 1);

	PG_RETURN_TSVECTOR2(result);
}

PG_FUNCTION_INFO_V1(json_string_to_tsvector2);
Datum
json_string_to_tsvector2(PG_FUNCTION_ARGS)
{
	text	   *json = PG_GETARG_TEXT_P(0);
	Oid			cfgId;
	TSVector2	result;

	cfgId = getTSCurrentConfig(true);
	result = json_to_tsvector2_worker(cfgId, json, jtiString);
	PG_FREE_IF_COPY(json, 0);

	PG_RETURN_TSVECTOR2(result);
}

PG_FUNCTION_INFO_V1(json_to_tsvector2_byid);
Datum
json_to_tsvector2_byid(PG_FUNCTION_ARGS)
{
	Oid			cfgId = PG_GETARG_OID(0);
	text	   *json = PG_GETARG_TEXT_P(1);
	Jsonb	   *jbFlags = PG_GETARG_JSONB_P(2);
	TSVector2	result;
	uint32		flags = 0;

#if PG_VERSION_NUM >= 110000
	flags = parse_jsonb_index_flags(jbFlags);
#endif

	result = json_to_tsvector2_worker(cfgId, json, flags);
	PG_FREE_IF_COPY(json, 1);
	PG_FREE_IF_COPY(jbFlags, 2);

	PG_RETURN_TSVECTOR2(result);
}

PG_FUNCTION_INFO_V1(json_to_tsvector2);
Datum
json_to_tsvector2(PG_FUNCTION_ARGS)
{
	text	   *json = PG_GETARG_TEXT_P(0);
	Jsonb	   *jbFlags = PG_GETARG_JSONB_P(1);
	Oid			cfgId;
	TSVector2	result;
	uint32		flags = 0;

#if PG_VERSION_NUM >= 110000
	flags = parse_jsonb_index_flags(jbFlags);
#endif

	cfgId = getTSCurrentConfig(true);
	result = json_to_tsvector2_worker(cfgId, json, flags);
	PG_FREE_IF_COPY(json, 0);
	PG_FREE_IF_COPY(jbFlags, 1);

	PG_RETURN_TSVECTOR2(result);
}

/*
 * to_tsvector
 */
static int
compareWORD(const void *a, const void *b)
{
	int			res;

	res = ts2_compare_string(
						  ((const ParsedWord *) a)->word, ((const ParsedWord *) a)->len,
						  ((const ParsedWord *) b)->word, ((const ParsedWord *) b)->len,
						  false);

	if (res == 0)
	{
		if (((const ParsedWord *) a)->pos.pos == ((const ParsedWord *) b)->pos.pos)
			return 0;

		res = (((const ParsedWord *) a)->pos.pos > ((const ParsedWord *) b)->pos.pos) ? 1 : -1;
	}

	return res;
}

static int
uniqueWORD(ParsedWord *a, int32 l)
{
	ParsedWord *ptr,
			   *res;
	int			tmppos;

	if (l == 1)
	{
		tmppos = LIMITPOS(a->pos.pos);
		a->alen = 2;
		a->pos.apos = (uint16 *) palloc(sizeof(uint16) * a->alen);
		a->pos.apos[0] = 1;
		a->pos.apos[1] = tmppos;
		return l;
	}

	res = a;
	ptr = a + 1;

	/*
	 * Sort words with its positions
	 */
	qsort((void *) a, l, sizeof(ParsedWord), compareWORD);

	/*
	 * Initialize first word and its first position
	 */
	tmppos = LIMITPOS(a->pos.pos);
	a->alen = 2;
	a->pos.apos = (uint16 *) palloc(sizeof(uint16) * a->alen);
	a->pos.apos[0] = 1;
	a->pos.apos[1] = tmppos;

	/*
	 * Summarize position information for each word
	 */
	while (ptr - a < l)
	{
		if (!(ptr->len == res->len &&
			  strncmp(ptr->word, res->word, res->len) == 0))
		{
			/*
			 * Got a new word, so put it in result
			 */
			res++;
			res->len = ptr->len;
			res->word = ptr->word;
			tmppos = LIMITPOS(ptr->pos.pos);
			res->alen = 2;
			res->pos.apos = (uint16 *) palloc(sizeof(uint16) * res->alen);
			res->pos.apos[0] = 1;
			res->pos.apos[1] = tmppos;
		}
		else
		{
			/*
			 * The word already exists, so adjust position information. But
			 * before we should check size of position's array, max allowed
			 * value for position and uniqueness of position
			 */
			pfree(ptr->word);
			if (res->pos.apos[0] < MAXNUMPOS - 1 && res->pos.apos[res->pos.apos[0]] != MAXENTRYPOS - 1 &&
				res->pos.apos[res->pos.apos[0]] != LIMITPOS(ptr->pos.pos))
			{
				if (res->pos.apos[0] + 1 >= res->alen)
				{
					res->alen *= 2;
					res->pos.apos = (uint16 *) repalloc(res->pos.apos, sizeof(uint16) * res->alen);
				}
				if (res->pos.apos[0] == 0 || res->pos.apos[res->pos.apos[0]] != LIMITPOS(ptr->pos.pos))
				{
					res->pos.apos[res->pos.apos[0] + 1] = LIMITPOS(ptr->pos.pos);
					res->pos.apos[0]++;
				}
			}
		}
		ptr++;
	}

	return res + 1 - a;
}

/*
 * Parse lexemes in an element of a json(b) value, add to TSVectorBuildState.
 */
static void
add_to_tsvector2(void *_state, char *elem_value, int elem_len)
{
	TSVectorBuildState *state = (TSVectorBuildState *) _state;
	ParsedText *prs = state->prs;
	int32		prevwords;

	if (prs->words == NULL)
	{
		/*
		 * First time through: initialize words array to a reasonable size.
		 * (parsetext() will realloc it bigger as needed.)
		 */
		prs->lenwords = 16;
		prs->words = (ParsedWord *) palloc(sizeof(ParsedWord) * prs->lenwords);
		prs->curwords = 0;
		prs->pos = 0;
	}

	prevwords = prs->curwords;

	parsetext(state->cfgId, prs, elem_value, elem_len);

	/*
	 * If we extracted any words from this JSON element, advance pos to create
	 * an artificial break between elements.  This is because we don't want
	 * phrase searches to think that the last word in this element is adjacent
	 * to the first word in the next one.
	 */
	if (prs->curwords > prevwords)
		prs->pos += 1;
}

/*
 * make value of tsvector, given parsed text
 *
 * Note: frees prs->words and subsidiary data.
 */
TSVector2
make_tsvector2(void *prs1)
{
	int			i,
				lenstr = 0,
				totallen,
				stroff = 0;
	TSVector2	in;
	ParsedText *prs = (ParsedText *) prs1;

	/* Merge duplicate words */
	if (prs->curwords > 0)
		prs->curwords = uniqueWORD(prs->words, prs->curwords);

	/* Determine space needed */
	for (i = 0; i < prs->curwords; i++)
	{
		int			npos = prs->words[i].alen ? prs->words[i].pos.apos[0] : 0;

		INCRSIZE(lenstr, i, prs->words[i].len, npos);
	}

	if (lenstr > MAXSTRPOS)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("string is too long for tsvector (%d bytes, max %d bytes)",
					 lenstr, MAXSTRPOS)));

	totallen = CALCDATASIZE(prs->curwords, lenstr);
	in = (TSVector2) palloc0(totallen);
	SET_VARSIZE(in, totallen);
	in->size = prs->curwords;

	for (i = 0; i < prs->curwords; i++)
	{
		int			npos = 0;

		if (prs->words[i].alen)
			npos = prs->words[i].pos.apos[0];

		tsvector2_addlexeme(in, i, &stroff, prs->words[i].word, prs->words[i].len,
						   prs->words[i].pos.apos + 1, npos);

		pfree(prs->words[i].word);
		if (prs->words[i].alen)
			pfree(prs->words[i].pos.apos);
	}

	if (prs->words)
		pfree(prs->words);

	return in;
}

PG_FUNCTION_INFO_V1(to_tsvector2_byid);
Datum
to_tsvector2_byid(PG_FUNCTION_ARGS)
{
	Oid			cfgId = PG_GETARG_OID(0);
	text	   *in = PG_GETARG_TEXT_PP(1);
	ParsedText	prs;
	TSVector2	out;

	prs.lenwords = VARSIZE_ANY_EXHDR(in) / 6;	/* just estimation of word's
												 * number */
	if (prs.lenwords < 2)
		prs.lenwords = 2;
	prs.curwords = 0;
	prs.pos = 0;
	prs.words = (ParsedWord *) palloc(sizeof(ParsedWord) * prs.lenwords);

	parsetext(cfgId, &prs, VARDATA_ANY(in), VARSIZE_ANY_EXHDR(in));

	PG_FREE_IF_COPY(in, 1);

	out = make_tsvector2(&prs);
	PG_RETURN_TSVECTOR2(out);
}

PG_FUNCTION_INFO_V1(to_tsvector2);
Datum
to_tsvector2(PG_FUNCTION_ARGS)
{
	text	   *in = PG_GETARG_TEXT_PP(0);
	Oid			cfgId;

	cfgId = getTSCurrentConfig(true);
	PG_RETURN_DATUM(DirectFunctionCall2(to_tsvector2_byid,
										ObjectIdGetDatum(cfgId),
										PointerGetDatum(in)));
}
