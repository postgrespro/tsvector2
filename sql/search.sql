-- without indexes
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'wr|qh';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'wr&qh';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'eq&yt';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'eq|yt';
SELECT count(*) FROM test_tsvector2 WHERE a @@ '(eq&yt)|(wr&qh)';
SELECT count(*) FROM test_tsvector2 WHERE a @@ '(eq|yt)&(wr|qh)';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'w:*|q:*';
SELECT count(*) FROM test_tsvector2 WHERE a @@ any ('{wr,qh}');
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'no_such_lexeme';
SELECT count(*) FROM test_tsvector2 WHERE a @@ '!no_such_lexeme';

create index wowidx on test_tsvector2 using gist (a);

SET enable_seqscan=OFF;
SET enable_indexscan=ON;
SET enable_bitmapscan=OFF;

explain (costs off) SELECT count(*) FROM test_tsvector2 WHERE a @@ 'wr|qh';

SELECT count(*) FROM test_tsvector2 WHERE a @@ 'wr|qh';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'wr&qh';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'eq&yt';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'eq|yt';
SELECT count(*) FROM test_tsvector2 WHERE a @@ '(eq&yt)|(wr&qh)';
SELECT count(*) FROM test_tsvector2 WHERE a @@ '(eq|yt)&(wr|qh)';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'w:*|q:*';
SELECT count(*) FROM test_tsvector2 WHERE a @@ any ('{wr,qh}');
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'no_such_lexeme';
SELECT count(*) FROM test_tsvector2 WHERE a @@ '!no_such_lexeme';

SET enable_indexscan=OFF;
SET enable_bitmapscan=ON;

explain (costs off) SELECT count(*) FROM test_tsvector2 WHERE a @@ 'wr|qh';

SELECT count(*) FROM test_tsvector2 WHERE a @@ 'wr|qh';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'wr&qh';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'eq&yt';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'eq|yt';
SELECT count(*) FROM test_tsvector2 WHERE a @@ '(eq&yt)|(wr&qh)';
SELECT count(*) FROM test_tsvector2 WHERE a @@ '(eq|yt)&(wr|qh)';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'w:*|q:*';
SELECT count(*) FROM test_tsvector2 WHERE a @@ any ('{wr,qh}');
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'no_such_lexeme';
SELECT count(*) FROM test_tsvector2 WHERE a @@ '!no_such_lexeme';

RESET enable_seqscan;
RESET enable_indexscan;
RESET enable_bitmapscan;

DROP INDEX wowidx;

SET enable_seqscan=OFF;

CREATE INDEX wowidx ON test_tsvector2 USING btree(a);
SELECT count(*) FROM test_tsvector2 WHERE a > 'lq d7 i4 7w y0 qt gw ch o6 eo';
SELECT count(*) FROM test_tsvector2 WHERE a < 'lq d7 i4 7w y0 qt gw ch o6 eo';
SELECT count(*) FROM test_tsvector2 WHERE a >= 'lq d7 i4 7w y0 qt gw ch o6 eo';
SELECT count(*) FROM test_tsvector2 WHERE a <= 'lq d7 i4 7w y0 qt gw ch o6 eo';
SELECT count(*) FROM test_tsvector2 WHERE a = 'lq d7 i4 7w y0 qt gw ch o6 eo';
SELECT count(*) FROM test_tsvector2 WHERE a <> 'lq d7 i4 7w y0 qt gw ch o6 eo';
EXPLAIN (COSTS OFF) SELECT count(*) FROM test_tsvector2 WHERE a <> 'lq d7 i4 7w y0 qt gw ch o6 eo';
DROP INDEX wowidx;

CREATE INDEX wowidx ON test_tsvector2 USING gin (a);
-- GIN only supports bitmapscan, so no need to test plain indexscan

EXPLAIN (COSTS OFF) SELECT count(*) FROM test_tsvector2 WHERE a @@ 'wr|qh';

SELECT count(*) FROM test_tsvector2 WHERE a @@ 'wr|qh';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'wr&qh';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'eq&yt';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'eq|yt';
SELECT count(*) FROM test_tsvector2 WHERE a @@ '(eq&yt)|(wr&qh)';
SELECT count(*) FROM test_tsvector2 WHERE a @@ '(eq|yt)&(wr|qh)';
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'w:*|q:*';
SELECT count(*) FROM test_tsvector2 WHERE a @@ any ('{wr,qh}');
SELECT count(*) FROM test_tsvector2 WHERE a @@ 'no_such_lexeme';
SELECT count(*) FROM test_tsvector2 WHERE a @@ '!no_such_lexeme';

SELECT * FROM tsvector2_stat('SELECT a FROM test_tsvector2') ORDER BY ndoc DESC, nentry DESC, word LIMIT 10;
SELECT * FROM tsvector2_stat('SELECT a FROM test_tsvector2', 'AB') ORDER BY ndoc DESC, nentry DESC, word;

RESET enable_seqscan;
DROP TABLE test_tsvector2 CASCADE;
DROP EXTENSION tsvector2 CASCADE;
