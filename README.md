tsvector2
==========

Extended tsvector type for PostgreSQL 10+. It was implemented to provide better
compression and to remove 1MB size limitation of original tsvector type.

It could be used as transparent replacement of original tsvector and supports
all its functions and index types. Functions that contain `tsvector` in their
names have been changed to `tsvector2`. Full list of these functions specified
below.

tsvector2 specific functions
-----------------------------

* `to_tsvector2` (from `text`, `json`, `jsonb` types)
* `array_to_tsvector2`
* `tsvector2_to_array`
* `tsvector2_stat` (should be used instead of `ts_stat`)
* `jsonb_to_tsvector2`
* `json_to_tsvector2`
