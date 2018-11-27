CREATE FUNCTION rum_extract_tsvector2(tsvector2,internal,internal,internal,internal)
	RETURNS internal
	AS 'MODULE_PATHNAME'
	LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION rum_extract_tsvector2_hash(tsvector2,internal,internal,internal,internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION install_rum_for_tsvector2()
RETURNS VOID AS $$
DECLARE found BOOL;
BEGIN
	CREATE FUNCTION rum_ts_distance(tsvector2,tsquery)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'rum_ts_distance_tt'
	LANGUAGE C IMMUTABLE STRICT;

	CREATE FUNCTION rum_ts_distance(tsvector2,tsquery,int)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'rum_ts_distance_ttf'
	LANGUAGE C IMMUTABLE STRICT;

	CREATE FUNCTION rum_ts_distance(tsvector2,rum_distance_query)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'rum_ts_distance_td'
	LANGUAGE C IMMUTABLE STRICT;

	CREATE FUNCTION rum_ts_score(tsvector2,tsquery)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'rum_ts_score_tt'
	LANGUAGE C IMMUTABLE STRICT;

	CREATE FUNCTION rum_ts_score(tsvector2,tsquery,int)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'rum_ts_score_ttf'
	LANGUAGE C IMMUTABLE STRICT;

	CREATE FUNCTION rum_ts_score(tsvector2,rum_distance_query)
	RETURNS float4
	AS 'MODULE_PATHNAME', 'rum_ts_score_td'
	LANGUAGE C IMMUTABLE STRICT;

	CREATE OPERATOR <=> (
			LEFTARG = tsvector2,
			RIGHTARG = tsquery,
			PROCEDURE = rum_ts_distance
	);

	CREATE OPERATOR <=> (
			LEFTARG = tsvector2,
			RIGHTARG = rum_distance_query,
			PROCEDURE = rum_ts_distance
	);

	CREATE OPERATOR CLASS rum_tsvector2_ops
	DEFAULT FOR TYPE tsvector2 USING rum
	AS
		OPERATOR 1   @@ (tsvector2, tsquery),
		OPERATOR 2   <=> (tsvector2, tsquery) FOR ORDER BY pg_catalog.float_ops,
		FUNCTION 1   gin_cmp_tslexeme(text, text),
		FUNCTION 2   rum_extract_tsvector2(tsvector2,internal,internal,internal,internal),
		FUNCTION 3   rum_extract_tsquery(tsquery,internal,smallint,internal,internal,internal,internal),
		FUNCTION 4   rum_tsquery_consistent(internal,smallint,tsvector,int,internal,internal,internal,internal),
		FUNCTION 5   gin_cmp_prefix(text,text,smallint,internal),
		FUNCTION 6   rum_tsvector_config(internal),
		FUNCTION 7   rum_tsquery_pre_consistent(internal,smallint,tsvector,int,internal,internal,internal,internal),
		FUNCTION 8   rum_tsquery_distance(internal,smallint,tsvector,int,internal,internal,internal,internal,internal),
		FUNCTION 10  rum_ts_join_pos(internal, internal),
		STORAGE  text;

	CREATE OPERATOR CLASS rum_tsvector2_hash_ops
	FOR TYPE tsvector2 USING rum
	AS
			OPERATOR        1       @@ (tsvector2, tsquery),
			OPERATOR        2       <=> (tsvector2, tsquery) FOR ORDER BY pg_catalog.float_ops,
			FUNCTION        1       btint4cmp(integer, integer),
			FUNCTION        2       rum_extract_tsvector2_hash(tsvector2,internal,internal,internal,internal),
			FUNCTION        3       rum_extract_tsquery_hash(tsquery,internal,smallint,internal,internal,internal,internal),
			FUNCTION        4       rum_tsquery_consistent(internal,smallint,tsvector,int,internal,internal,internal,internal),
			FUNCTION        6       rum_tsvector_config(internal),
			FUNCTION        7       rum_tsquery_pre_consistent(internal,smallint,tsvector,int,internal,internal,internal,internal),
			FUNCTION        8       rum_tsquery_distance(internal,smallint,tsvector,int,internal,internal,internal,internal,internal),
			FUNCTION        10      rum_ts_join_pos(internal, internal),
			STORAGE         integer;

	CREATE OPERATOR CLASS rum_tsvector2_addon_ops
	FOR TYPE tsvector2 USING rum
	AS
		OPERATOR	1	@@ (tsvector2, tsquery),
		--support function
		FUNCTION	1	gin_cmp_tslexeme(text, text),
		FUNCTION	2	rum_extract_tsvector2(tsvector2,internal,internal,internal,internal),
		FUNCTION	3	rum_extract_tsquery(tsquery,internal,smallint,internal,internal,internal,internal),
		FUNCTION	4	rum_tsquery_addon_consistent(internal,smallint,tsvector,int,internal,internal,internal,internal),
		FUNCTION	5	gin_cmp_prefix(text,text,smallint,internal),
		FUNCTION	7	rum_tsquery_pre_consistent(internal,smallint,tsvector,int,internal,internal,internal,internal),
		STORAGE	 text;

	CREATE OPERATOR CLASS rum_tsvector2_hash_addon_ops
	FOR TYPE tsvector2 USING rum
	AS
		OPERATOR	1	@@ (tsvector2, tsquery),
		--support function
		FUNCTION	1	btint4cmp(integer, integer),
		FUNCTION	2	rum_extract_tsvector2_hash(tsvector2,internal,internal,internal,internal),
		FUNCTION	3	rum_extract_tsquery_hash(tsquery,internal,smallint,internal,internal,internal,internal),
		FUNCTION	4	rum_tsquery_addon_consistent(internal,smallint,tsvector,int,internal,internal,internal,internal),
		FUNCTION	7	rum_tsquery_pre_consistent(internal,smallint,tsvector,int,internal,internal,internal,internal),
		STORAGE	 integer;
	END;

$$  LANGUAGE plpgsql;

-- rum support if it's installed
DO LANGUAGE plpgsql $$
BEGIN
	IF EXISTS (SELECT 1 FROM pg_am WHERE amname = 'rum') THEN
		PERFORM install_rum_for_tsvector2();
	END IF;
END$$;
