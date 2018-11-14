/*-------------------------------------------------------------------------
 *
 * tsvector2_gin.c
 *	 GIN support functions for tsvector2
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 2018, PostgresPro
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/gin.h"
#include "access/stratnum.h"
#include "miscadmin.h"
#include "tsearch/ts_type.h"
#include "tsearch/ts_utils.h"
#include "utils/builtins.h"

#include "tsvector2.h"

PG_FUNCTION_INFO_V1(gin_extract_tsvector2);
Datum
gin_extract_tsvector2(PG_FUNCTION_ARGS)
{
	TSVector2	vector = PG_GETARG_TSVECTOR2(0);
	int32	   *nentries = (int32 *) PG_GETARG_POINTER(1);
	Datum	   *entries = NULL;

	*nentries = vector->size;
	if (vector->size > 0)
	{
		int			i;
		uint32		pos;

		WordEntry2  *we = tsvector2_entries(vector);

		entries = (Datum *) palloc(sizeof(Datum) * vector->size);

		INITPOS(pos);
		for (i = 0; i < vector->size; i++)
		{
			text	   *txt;

			txt = cstring_to_text_with_len(tsvector2_storage(vector) + pos,
										   ENTRY_LEN(vector, we));
			entries[i] = PointerGetDatum(txt);
			INCRPTR(vector, we, pos);
		}
	}

	PG_FREE_IF_COPY(vector, 0);
	PG_RETURN_POINTER(entries);
}
