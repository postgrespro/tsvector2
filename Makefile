MODULE_big = tsvector2
OBJS = src/tsvector2.o src/tsvector2_op.o src/tsvector2_rank.o \
	   src/tsvector2_conv.o

EXTENSION = tsvector2
EXTVERSION = 1.0
DATA_built = tsvector2--$(EXTVERSION).sql

REGRESS = operators functions match rank json

EXTRA_CLEAN = tsvector2--$(EXTVERSION).sql

override PG_CPPFLAGS += -I$(CURDIR)/src
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

$(EXTENSION)--$(EXTVERSION).sql: init.sql
	cat $^ > $@
