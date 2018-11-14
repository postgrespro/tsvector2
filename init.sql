-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION tsvector2" to load this file. \quit

CREATE TYPE tsvector2;

CREATE FUNCTION tsvector2in(cstring)
	RETURNS tsvector2
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsvector2out(tsvector2)
	RETURNS cstring
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsvector2recv(internal)
	RETURNS tsvector2
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsvector2send(tsvector2)
	RETURNS bytea
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsvector2_concat(tsvector2, tsvector2)
	RETURNS tsvector2
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE TYPE tsvector2 (
	INPUT = tsvector2in,
	OUTPUT = tsvector2out,
	RECEIVE = tsvector2recv,
	SEND = tsvector2send,
	INTERNALLENGTH = -1,
	STORAGE = EXTENDED
);

-- functions
CREATE FUNCTION tsvector2_lt(tsvector2, tsvector2)
	RETURNS bool
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsvector2_le(tsvector2, tsvector2)
	RETURNS bool
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsvector2_eq(tsvector2, tsvector2)
	RETURNS bool
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsvector2_ne(tsvector2, tsvector2)
	RETURNS bool
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsvector2_ge(tsvector2, tsvector2)
	RETURNS bool
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsvector2_gt(tsvector2, tsvector2)
	RETURNS bool
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsvector2_cmp(tsvector2, tsvector2)
	RETURNS int
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsvector2_match_tsquery(tsvector2, tsquery)
	RETURNS bool
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsquery_match_tsvector2(tsquery, tsvector2)
	RETURNS bool
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

-- operators
CREATE OPERATOR < (
	LEFTARG = tsvector2,
	RIGHTARG = tsvector2,
	PROCEDURE = tsvector2_lt,
	COMMUTATOR = '>',
	NEGATOR = '>=',
	RESTRICT = scalarltsel,
	JOIN = scalarltjoinsel
);

CREATE OPERATOR <= (
	LEFTARG = tsvector2,
	RIGHTARG = tsvector2,
	PROCEDURE = tsvector2_le,
	COMMUTATOR = '>=',
	NEGATOR = '>',
	RESTRICT = scalarltsel,
	JOIN = scalarltjoinsel
);

CREATE OPERATOR = (
	LEFTARG = tsvector2,
	RIGHTARG = tsvector2,
	PROCEDURE = tsvector2_eq,
	COMMUTATOR = '=',
	NEGATOR = '<>',
	RESTRICT = eqsel,
	JOIN = eqjoinsel,
	HASHES,
	MERGES
);

CREATE OPERATOR <> (
	LEFTARG = tsvector2,
	RIGHTARG = tsvector2,
	PROCEDURE = tsvector2_ne,
	COMMUTATOR = '<>',
	NEGATOR = '=',
	RESTRICT = neqsel,
	JOIN = neqjoinsel
);

CREATE OPERATOR >= (
	LEFTARG = tsvector2,
	RIGHTARG = tsvector2,
	PROCEDURE = tsvector2_ge,
	COMMUTATOR = '<=',
	NEGATOR = '<',
	RESTRICT = scalargtsel,
	JOIN = scalargtjoinsel
);

CREATE OPERATOR > (
	LEFTARG = tsvector2,
	RIGHTARG = tsvector2,
	PROCEDURE = tsvector2_gt,
	COMMUTATOR = '<',
	NEGATOR = '<=',
	RESTRICT = scalargtsel,
	JOIN = scalargtjoinsel
);

CREATE OPERATOR || (
	LEFTARG = tsvector2,
	RIGHTARG = tsvector2,
	PROCEDURE = tsvector2_concat
);

CREATE OPERATOR @@ (
	LEFTARG = tsvector2,
	RIGHTARG = tsquery,
	PROCEDURE = tsvector2_match_tsquery,
	COMMUTATOR = '@@'
);

CREATE OPERATOR @@ (
	LEFTARG = tsquery,
	RIGHTARG = tsvector2,
	PROCEDURE = tsquery_match_tsvector2,
	COMMUTATOR = '@@'
);

-- tsvector2 functions
CREATE FUNCTION length(tsvector2)
	RETURNS int
	AS 'MODULE_PATHNAME', 'tsvector2_length'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION strip(tsvector2)
	RETURNS tsvector2
	AS 'MODULE_PATHNAME', 'tsvector2_strip'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION unnest(tsvector2, OUT lexeme text, OUT positions smallint[],
								  OUT weights text[])
	RETURNS SETOF RECORD
	AS 'MODULE_PATHNAME', 'tsvector2_unnest'
	LANGUAGE C STRICT IMMUTABLE;

/* TODO: documentation should mention that ts_stat is not applicable to tsvector2 */
CREATE FUNCTION tsvector2_stat(sqlquery text, OUT word text, OUT ndoc integer,
	OUT nentry integer)
	RETURNS SETOF RECORD
	AS 'MODULE_PATHNAME', 'tsvector2_stat1'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsvector2_stat(sqlquery text, weights text,
	OUT word text, OUT ndoc integer, OUT nentry integer)
	RETURNS SETOF RECORD
	AS 'MODULE_PATHNAME', 'tsvector2_stat2'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION setweight(tsvector2, "char")
	RETURNS tsvector2
	AS 'MODULE_PATHNAME', 'tsvector2_setweight'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION setweight(tsvector2, "char", text[])
	RETURNS tsvector2
	AS 'MODULE_PATHNAME', 'tsvector2_setweight_by_filter'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION to_tsvector2(text)
	RETURNS tsvector2
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION to_tsvector2(regconfig, text)
	RETURNS tsvector2
	AS 'MODULE_PATHNAME', 'to_tsvector2_byid'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION ts_filter(tsvector2, "char"[])
	RETURNS tsvector2
	AS 'MODULE_PATHNAME', 'tsvector2_filter'
	LANGUAGE C STRICT IMMUTABLE;

-- array conversion
CREATE FUNCTION array_to_tsvector2(text[])
	RETURNS tsvector2
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION tsvector2_to_array(tsvector2)
	RETURNS text[]
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

-- ts_delete
CREATE FUNCTION ts_delete(tsvector2, text[])
	RETURNS tsvector2
	AS 'MODULE_PATHNAME', 'tsvector2_delete_arr'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION ts_delete(tsvector2, text)
	RETURNS tsvector2
	AS 'MODULE_PATHNAME', 'tsvector2_delete_str'
	LANGUAGE C STRICT IMMUTABLE;

-- ts_rank_cd
CREATE FUNCTION ts_rank_cd(float4[], tsvector2, tsquery, int4)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'tsvector2_rankcd_wttf'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION ts_rank_cd(float4[], tsvector2, tsquery)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'tsvector2_rankcd_wtt'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION ts_rank_cd(tsvector2, tsquery, int4)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'tsvector2_rankcd_ttf'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION ts_rank_cd(tsvector2, tsquery)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'tsvector2_rankcd_tt'
	LANGUAGE C STRICT IMMUTABLE;

-- ts_rank
CREATE FUNCTION ts_rank(float4[], tsvector2, tsquery, int4)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'tsvector2_rank_wttf'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION ts_rank(float4[], tsvector2, tsquery)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'tsvector2_rank_wtt'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION ts_rank(tsvector2, tsquery, int4)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'tsvector2_rank_ttf'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION ts_rank(tsvector2, tsquery)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'tsvector2_rank_tt'
	LANGUAGE C STRICT IMMUTABLE;

-- operator families for various types of indexes
CREATE OPERATOR FAMILY tsvector2_ops USING btree;
CREATE OPERATOR FAMILY tsvector2_ops USING gist;
CREATE OPERATOR FAMILY tsvector2_ops USING gin;

-- btree support
CREATE OPERATOR CLASS bt_tsvector2_ops DEFAULT
	FOR TYPE tsvector2 USING btree FAMILY tsvector2_ops AS
	OPERATOR 1  <  (tsvector2, tsvector2),
	OPERATOR 2  <= (tsvector2, tsvector2),
	OPERATOR 3  =  (tsvector2, tsvector2),
	OPERATOR 4  >= (tsvector2, tsvector2),
	OPERATOR 5  >  (tsvector2, tsvector2),
	FUNCTION 1  tsvector2_cmp(tsvector2, tsvector2);

-- gin support
CREATE FUNCTION gin_extract_tsvector2(tsvector2, internal, internal)
	RETURNS internal
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE OPERATOR CLASS gin_tsvector2_ops DEFAULT
	FOR TYPE tsvector2 USING gin FAMILY tsvector2_ops AS
	OPERATOR 1  @@ (tsvector2, tsquery),
	FUNCTION 1  gin_cmp_tslexeme(text,text),
	FUNCTION 2  gin_extract_tsvector2(tsvector2,internal,internal),
	FUNCTION 3  gin_extract_tsquery(tsquery,internal,int2,internal,internal,internal,internal),
	FUNCTION 4  gin_tsquery_consistent(internal,int2,tsquery,int4,internal,internal,internal,internal),
	FUNCTION 5  gin_cmp_prefix(text,text,int2,internal),
	FUNCTION 6  gin_tsquery_triconsistent;

-- gist support
