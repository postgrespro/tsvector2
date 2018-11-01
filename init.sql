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
