MODULE_big = tsvector2
OBJS = src/tsvector.o src/tsvector_op.o

EXTENSION = tsvector2
EXTVERSION = 1.0
DATA_built = tsvector2--$(EXTVERSION).sql

REGRESS = basic

EXTRA_CLEAN = tsvector2--$(EXTVERSION).sql

override PG_CPPFLAGS += -I$(CURDIR)/src/include
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

$(EXTENSION)--$(EXTVERSION).sql: init.sql
	cat $^ > $@
