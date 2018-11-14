CREATE SCHEMA test;
CREATE EXTENSION tsvector2 SCHEMA test;
CREATE EXTENSION tsvector2;
CREATE TABLE test.t1(a tsvector2);

-- use only indexes if possible
SET enable_seqscan = off;

CREATE OR REPLACE FUNCTION test.insert_records()
RETURNS VOID AS $$
DECLARE
	x int;
BEGIN
	FOR x IN
		SELECT i FROM generate_series(1, 1000) i
	LOOP
		INSERT INTO test.t1(a)
			SELECT array_to_tsvector2(array_agg(repeat(letter, letters_count)))
			FROM (
				SELECT chr(i) AS letter, b AS letters_count
				FROM generate_series(ascii('a'), ascii('z')) i
				FULL OUTER JOIN
				(SELECT b FROM generate_series(3, 12) b) t2
				ON true
			) t3;
	END LOOP;
END
$$ LANGUAGE plpgsql;

SELECT test.insert_records();

-- btree
CREATE INDEX i1 ON test.t1 USING btree(a);
EXPLAIN (COSTS OFF) SELECT * FROM test.t1 WHERE a >= 'a:1 b:2'::tsvector2;
SELECT sum(length(a)) FROM test.t1 WHERE a >= 'a:1 b:2'::tsvector2;
DROP INDEX test.i1;

-- gin
CREATE INDEX i2 ON test.t1 USING gin(a);
EXPLAIN (COSTS OFF) SELECT * FROM test.t1 WHERE a @@ 'zzz';
SELECT sum(length(a)) FROM test.t1 WHERE a @@ 'zzz';
DROP INDEX test.i2;

DROP SCHEMA test CASCADE;
DROP EXTENSION tsvector2 CASCADE;
