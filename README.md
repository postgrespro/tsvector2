[![Build Status](https://travis-ci.org/postgrespro/tsvector2.svg?branch=master)](https://travis-ci.org/postgrespro/tsvector2)
[![GitHub license](https://img.shields.io/badge/license-PostgreSQL-blue.svg)](https://raw.githubusercontent.com/postgrespro/tsvector2/master/LICENSE)

tsvector2
==========

Extended `tsvector` type for PostgreSQL 9.6+. It was implemented to provide better
compression and to remove 1MB size limitation of original tsvector type.

It could be used as transparent replacement of original tsvector and supports
all its functions, operators and index types. Functions that contain `tsvector` in their
names have been changed to `tsvector2`. Full list of these functions specified
below.

Refer to PostgreSQL [documentation](https://www.postgresql.org/docs/current/datatype-textsearch.html)
to get details about `tsvector`. They can be also applied to `tsvector2`.

`tsvector2` specific functions
------------------------------

* `to_tsvector2` (from `text`, `json`, `jsonb` types)
* `array_to_tsvector2`
* `tsvector2_to_array`
* `tsvector2_stat` (should be used instead of `ts_stat`)
* `jsonb_to_tsvector2`
* `json_to_tsvector2`
* `tsvector2_update_trigger`
* `tsvector2_update_trigger_column`

Notice that `jsonb_to_tsvector2` and `json_to_tsvector2` work different on
PostgreSQL 10 and 11. Same applies to `tsvector` functions.

Common functions that could be safely used on both types
--------------------------------------------------------

* `strip`
* `unnest`
* `length`
* `setweight`
* `ts_rank`
* `ts_rank_cd`
* `ts_delete`
* `ts_filter`

Installation
-------------

	make install PG_CONFIG=<path_to_pg_config>
	psql <database> -c "create extension tsvector2"
