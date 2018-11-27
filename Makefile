MODULE_big = tsvector2
OBJS = src/tsvector2.o src/tsvector2_op.o src/tsvector2_rank.o \
	   src/tsvector2_conv.o src/tsvector2_gin.o src/tsvector2_gist.o \
	   src/tsvector2_sel.o src/tsvector2_rum.o

EXTENSION = tsvector2
EXTVERSION = 1.0
DATA_built = tsvector2--$(EXTVERSION).sql

REGRESS = operators functions match rank json index_data search rum rum_hash rum_tsvector

EXTRA_CLEAN = tsvector2--$(EXTVERSION).sql

override PG_CPPFLAGS += -I$(CURDIR)/src
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

$(EXTENSION)--$(EXTVERSION).sql: init.sql rum_support.sql
	cat $^ > $@
