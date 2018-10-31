MODULE_big = tsvector2
OBJS = src/tsvector2.o src/tsvector_op2.o

EXTENSION = tsvector2
EXTVERSION = 1.0
DATA_built = tsvector2--$(EXTVERSION).sql

REGRESS = basic

EXTRA_CLEAN = tsvector2--$(EXTVERSION).sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

$(EXTENSION)--$(EXTVERSION).sql: init.sql
	cat $^ > $@
