/*-------------------------------------------------------------------------
 *
 * tsvector2_gist.c
 *	  GiST support functions for tsvector2_ops
 *
 * Heavily based on tsgistidx.c from core.
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 2018, PostgresPro
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/gist.h"
#include "access/tuptoaster.h"
#include "utils/pg_crc.h"

#include "tsvector2.h"


#define SIGLENINT  31			/* >121 => key will toast, so it will not work
								 * !!! */

#define SIGLEN	( sizeof(int32) * SIGLENINT )
#define SIGLENBIT (SIGLEN * BITS_PER_BYTE)

typedef char BITVEC[SIGLEN];
typedef char *BITVECP;

#define LOOPBYTE \
			for(i=0;i<SIGLEN;i++)

#define GETBYTE(x,i) ( *( (BITVECP)(x) + (int)( (i) / BITS_PER_BYTE ) ) )
#define GETBITBYTE(x,i) ( ((char)(x)) >> (i) & 0x01 )
#define CLRBIT(x,i)   GETBYTE(x,i) &= ~( 0x01 << ( (i) % BITS_PER_BYTE ) )
#define SETBIT(x,i)   GETBYTE(x,i) |=  ( 0x01 << ( (i) % BITS_PER_BYTE ) )

#define HASHVAL(val) (((unsigned int)(val)) % SIGLENBIT)
#define HASH(sign, val) SETBIT((sign), HASHVAL(val))

#define GETENTRY(vec,pos) ((SignTSVector *) DatumGetPointer((vec)->vector[(pos)].key))

/*
 * type of GiST index key
 */

typedef struct
{
	int32		vl_len_;		/* varlena header (do not touch directly!) */
	int32		flag;
	char		data[FLEXIBLE_ARRAY_MEMBER];
} SignTSVector;

#define ARRKEY		0x01
#define SIGNKEY		0x02
#define ALLISTRUE	0x04

#define ISSIGNKEY(x)	( ((SignTSVector*)(x))->flag & SIGNKEY )
#define ISALLTRUE(x)	( ((SignTSVector*)(x))->flag & ALLISTRUE )

#define GTHDRSIZE	( VARHDRSZ + sizeof(int32) )
#define CALCGTSIZE(flag, len) ( GTHDRSIZE + ( ( (flag) & ARRKEY ) ? ((len)*sizeof(int32)) : (((flag) & ALLISTRUE) ? 0 : SIGLEN) ) )

#define GETSIGN(x)	( (BITVECP)( (char*)(x)+GTHDRSIZE ) )
#define GETARR(x)	( (int32*)( (char*)(x)+GTHDRSIZE ) )
#define ARRNELEM(x) ( ( VARSIZE(x) - GTHDRSIZE )/sizeof(int32) )

static int
compareint(const void *va, const void *vb)
{
	int32		a = *((const int32 *) va);
	int32		b = *((const int32 *) vb);

	if (a == b)
		return 0;
	return (a > b) ? 1 : -1;
}

/*
 * Removes duplicates from an array of int32. 'l' is
 * size of the input array. Returns the new size of the array.
 */
static int
uniqueint(int32 *a, int32 l)
{
	int32	   *ptr,
			   *res;

	if (l <= 1)
		return l;

	ptr = res = a;

	qsort((void *) a, l, sizeof(int32), compareint);

	while (ptr - a < l)
		if (*ptr != *res)
			*(++res) = *ptr++;
		else
			ptr++;
	return res + 1 - a;
}

static void
makesign(BITVECP sign, SignTSVector *a)
{
	int32		k,
				len = ARRNELEM(a);
	int32	   *ptr = GETARR(a);

	MemSet((void *) sign, 0, sizeof(BITVEC));
	for (k = 0; k < len; k++)
		HASH(sign, ptr[k]);
}

PG_FUNCTION_INFO_V1(gist_tsvector2_compress);
Datum
gist_tsvector2_compress(PG_FUNCTION_ARGS)
{
	GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
	GISTENTRY  *retval = entry;

	if (entry->leafkey)
	{							/* tsvector */
		SignTSVector *res;
		TSVector2	val = DatumGetTSVector2(entry->key);
		int32		len;
		int32	   *arr;
		WordEntry2  *ptr = tsvector2_entries(val);
		char	   *words = tsvector2_storage(val);
		const int	tscount = val->size;
		uint32		pos;

		len = CALCGTSIZE(ARRKEY, tscount);
		res = (SignTSVector *) palloc(len);
		SET_VARSIZE(res, len);
		res->flag = ARRKEY;
		arr = GETARR(res);
		len = tscount;

		INITPOS(pos);
		while (len--)
		{
			pg_crc32	c;

			INIT_LEGACY_CRC32(c);
			COMP_LEGACY_CRC32(c, words + pos, ENTRY_LEN(val, ptr));
			FIN_LEGACY_CRC32(c);

			*arr = *(int32 *) &c;
			arr++;

			INCRPTR(val, ptr, pos);
		}

		len = uniqueint(GETARR(res), tscount);
		if (len != tscount)
		{
			/*
			 * there is a collision of hash-function; len is always less than
			 * val->size
			 */
			len = CALCGTSIZE(ARRKEY, len);
			res = (SignTSVector *) repalloc((void *) res, len);
			SET_VARSIZE(res, len);
		}

		/* make signature, if array is too long */
		if (VARSIZE(res) > TOAST_INDEX_TARGET)
		{
			SignTSVector *ressign;

			len = CALCGTSIZE(SIGNKEY, 0);
			ressign = (SignTSVector *) palloc(len);
			SET_VARSIZE(ressign, len);
			ressign->flag = SIGNKEY;
			makesign(GETSIGN(ressign), res);
			res = ressign;
		}

		retval = (GISTENTRY *) palloc(sizeof(GISTENTRY));
		gistentryinit(*retval, PointerGetDatum(res),
					  entry->rel, entry->page,
					  entry->offset, false);
	}
	else if (ISSIGNKEY(DatumGetPointer(entry->key)) &&
			 !ISALLTRUE(DatumGetPointer(entry->key)))
	{
		int32		i,
					len;
		SignTSVector *res;
		BITVECP		sign = GETSIGN(DatumGetPointer(entry->key));

		LOOPBYTE
		{
			if ((sign[i] & 0xff) != 0xff)
				PG_RETURN_POINTER(retval);
		}

		len = CALCGTSIZE(SIGNKEY | ALLISTRUE, 0);
		res = (SignTSVector *) palloc(len);
		SET_VARSIZE(res, len);
		res->flag = SIGNKEY | ALLISTRUE;

		retval = (GISTENTRY *) palloc(sizeof(GISTENTRY));
		gistentryinit(*retval, PointerGetDatum(res),
					  entry->rel, entry->page,
					  entry->offset, false);
	}
	PG_RETURN_POINTER(retval);
}
