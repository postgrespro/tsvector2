/*-------------------------------------------------------------------------
 *
 * tsvector2.h
 *	  Definitions for the tsvector2 and tsquery types
 *
 * Copyright (c) 1998-2018, PostgreSQL Global Development Group
 * Copyright (c) 2018, PostgresPro
 *
 *-------------------------------------------------------------------------
 */
#ifndef TSVECTOR2_H
#define TSVECTOR2_H

#include "fmgr.h"
#include "utils/memutils.h"


/*
 * TSVector2 type.
 *
 * Structure of tsvector2 datatype:
 * 1) standard varlena header
 * 2) int32		size - number of lexemes (WordEntry2 array entries)
 * 3) Array of WordEntry2 - one per lexeme; must be sorted according to
 *				tsCompareString() (ie, memcmp of lexeme strings).
 *	  WordEntry2 have two types: offset or metadata (length of lexeme and number
 *	  of positions). If it has offset then metadata will be by this offset.
 * 4) Per-lexeme data storage:
 *    [4-byte aligned WordEntry2] (if its WordEntry2 has offset)
 *	  2-byte aligned lexeme string (not null-terminated)
 *	  if it has positions:
 *		padding byte if necessary to make the position data 2-byte aligned
 *		WordEntryPos[]	positions
 *
 * The positions for each lexeme must be sorted.
 *
 * Note, tsvector2 functions believe that sizeof(WordEntry2) == 4
 */

#define TS_OFFSET_STRIDE 4

typedef union
{
	struct
	{
		uint32		hasoff:1,
					offset:31;
	};
	struct
	{
		uint32		hasoff_:1,
					len:11,
					npos:16,
					_unused:4;
	};
} WordEntry2;

extern int	compareWordEntryPos(const void *a, const void *b);

/*
 * Equivalent to
 * typedef struct {
 *		uint16
 *			weight:2,
 *			pos:14;
 * }
 */

typedef uint16 WordEntryPos;


#define WEP_GETWEIGHT(x)	( (x) >> 14 )
#define WEP_GETPOS(x)		( (x) & 0x3fff )

#define WEP_SETWEIGHT(x,v)	( (x) = ( (v) << 14 ) | ( (x) & 0x3fff ) )
#define WEP_SETPOS(x,v)		( (x) = ( (x) & 0xc000 ) | ( (v) & 0x3fff ) )

#define MAXENTRYPOS (1<<14)
#define MAXNUMPOS	(256)
#define LIMITPOS(x) ( ( (x) >= MAXENTRYPOS ) ? (MAXENTRYPOS-1) : (x) )

/* This struct represents a complete tsvector2 datum */
typedef struct
{
	int32		vl_len;		/* varlena header (do not touch directly!) */
	int32		size;			/* flags and lexemes count */
	WordEntry2	entries[FLEXIBLE_ARRAY_MEMBER];
	/* lexemes follow the entries[] array */
} TSVectorData2;

typedef TSVectorData2 *TSVector2;

#define tsvector2_hdrlen() (offsetof(TSVectorData2, entries))
#define tsvector2_calcsize(nentries, lenstr) (tsvector2_hdrlen() + \
							(nentries) * sizeof(WordEntry2) + (lenstr) )

/* pointer to start of a tsvector2's WordEntry2 array */
#define tsvector2_entries(x)	( (x)->entries )

/* pointer to start of a tsvector2's lexeme storage */
#define tsvector2_storage(x)	( (char *) &(x)->entries[x->size] )

/* for WordEntry2 with offset return its WordEntry2 with other properties */
#define UNWRAP_ENTRY(x,we) \
	((we)->hasoff? (WordEntry2 *)(tsvector2_storage(x) + (we)->offset): (we))

/*
 * helpers used when we're not sure that WordEntry2
 * contains ether offset or len
 */
#define ENTRY_NPOS(x,we) (UNWRAP_ENTRY(x,we)->npos)
#define ENTRY_LEN(x,we) (UNWRAP_ENTRY(x,we)->len)

/* pointer to start of positions */
#define get_lexeme_positions(lex, len) ((WordEntryPos *) (lex + SHORTALIGN(len)))

/* set default offset in tsvector2 data */
#define INITPOS(p) ((p) = sizeof(WordEntry2))

/* increment entry and offset by given WordEntry2 */
#define INCRPTR(x,w,p) \
do { \
	WordEntry2 *y = (w);									\
	if ((w)->hasoff)									\
	{													\
		y = (WordEntry2 *) (tsvector2_storage(x) + (w)->offset);	\
		(p) = (w)->offset + sizeof(WordEntry2);			\
	}													\
	(w)++;												\
	Assert(!y->hasoff);									\
	(p) += SHORTALIGN(y->len) + y->npos * sizeof(WordEntryPos); \
	if ((w) - tsvector2_entries(x) < x->size && w->hasoff)		\
		(p) = INTALIGN(p) + sizeof(WordEntry2);			\
} while (0);

/* used to calculate tsvector2 size in in tsvector2 constructors */
#define INCRSIZE(s,i,l,n) /* size,index,len,npos */		\
do {													\
	if ((i) % TS_OFFSET_STRIDE == 0)					\
		(s) = INTALIGN(s) + sizeof(WordEntry2);			\
	else												\
		(s) = SHORTALIGN(s);							\
	(s) += (l);											\
	(s) = (n)? SHORTALIGN(s) + (n) * sizeof(WordEntryPos) : (s);	\
} while (0);

/*
 * fmgr interface macros
 */

TSVector2	tsvector2_upgrade(Datum orig, bool copy);

#define DatumGetTSVector2(X)		((TSVector2) PG_DETOAST_DATUM(X))
#define DatumGetTSVector2Copy(X)	((TSVector2) PG_DETOAST_DATUM(X))
#define TSVector2GetDatum(X)			PointerGetDatum(X)
#define PG_GETARG_TSVECTOR2(n)		DatumGetTSVector2(PG_GETARG_DATUM(n))
#define PG_GETARG_TSVECTOR2_COPY(n)	DatumGetTSVector2Copy(PG_GETARG_DATUM(n))
#define PG_RETURN_TSVECTOR2(x)		return TSVector2GetDatum(x)

extern int tsvector2_getoffset(TSVector2 vec, int idx, WordEntry2 **we);
extern char *tsvector2_addlexeme(TSVector2 tsv, int idx, int *dataoff,
	char *lexeme, int lexeme_len, WordEntryPos *pos, int npos);
extern int32 ts2_compare_string(char *a, int lena, char *b, int lenb, bool prefix);
extern TSVector2 make_tsvector2(void *prs1);

/* Returns lexeme and its entry by given index from TSVector2 */
inline static char *
tsvector2_getlexeme(TSVector2 vec, int idx, WordEntry2 **we)
{
	Assert(idx >= 0 && idx < vec->size);

	/*
	 * we do not allow we == NULL because returned lexeme is not \0 ended, and
	 * always should be used with we->len
	 */
	Assert(we != NULL);
	return tsvector2_storage(vec) + tsvector2_getoffset(vec, idx, we);
}

#ifndef PG_GETARG_JSONB_P
#define PG_GETARG_JSONB_P(x) PG_GETARG_JSONB(x)
#endif

#if PG_VERSION_NUM < 110000
typedef enum JsonToIndex
{
	jtiKey = 0x01,
	jtiString = 0x02,
	jtiNumeric = 0x04,
	jtiBool = 0x08,
	jtiAll = jtiKey | jtiString | jtiNumeric | jtiBool
} JsonToIndex;

#define init_tsvector_parser_compat(x,y) init_tsvector_parser(x,y,false)
#define pq_sendint32(x,y) pq_sendint(x,y,4)
#define pq_sendint16(x,y) pq_sendint(x,y,2)
#else
#define init_tsvector_parser_compat(x,y) init_tsvector_parser(x,y)
#endif

#if PG_VERSION_NUM >= 120000
#define CreateTemplateTupleDescCompat(a) CreateTemplateTupleDesc(a)
#else
#define CreateTemplateTupleDescCompat(a) CreateTemplateTupleDesc(a,false)
#endif

#ifndef TS_EXEC_CALC_NOT
#define TS_EXEC_CALC_NOT		(0x01)
#endif

#if PG_VERSION_NUM < 130000
typedef bool TSTernaryValue;
#define TS_YES true
#define TS_NO false
#endif

#endif
