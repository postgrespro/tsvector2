#ifndef TSVECTOR2_UTILS
#define TSVECTOR2_UTILS

#include "tsvector2.h"

#define RANK_NO_NORM			0x00
#define RANK_NORM_LOGLENGTH		0x01
#define RANK_NORM_LENGTH		0x02
#define RANK_NORM_EXTDIST		0x04
#define RANK_NORM_UNIQ			0x08
#define RANK_NORM_LOGUNIQ		0x10
#define RANK_NORM_RDIVRPLUS1	0x20
#define DEF_NORM_METHOD			RANK_NO_NORM

extern int count_length(TSVector2 t);
extern int find_wordentry(TSVector2 t, TSQuery q, QueryOperand *item, int32 *nitem);

#endif
