create extension tsvector2;

SELECT strip('w:12B w:13* w:12,5,6 a:1,3* a:3 w asd:1dc asd'::tsvector2);
SELECT strip('base:7 hidden:6 rebel:1 spaceship:2,33A,34B,35C,36D strike:3'::tsvector2);
SELECT strip('base hidden rebel spaceship strike'::tsvector2);

SELECT ts_delete(to_tsvector2('english', 'Rebel spaceships, striking from a hidden base'), 'spaceship');
SELECT ts_delete('base:7 hidden:6 rebel:1 spaceship:2,33A,34B,35C,36D strike:3'::tsvector2, 'base');
SELECT ts_delete('base:7 hidden:6 rebel:1 spaceship:2,33A,34B,35C,36D strike:3'::tsvector2, 'bas');
SELECT ts_delete('base:7 hidden:6 rebel:1 spaceship:2,33A,34B,35C,36D strike:3'::tsvector2, 'bases');
SELECT ts_delete('base:7 hidden:6 rebel:1 spaceship:2,33A,34B,35C,36D strike:3'::tsvector2, 'spaceship');
SELECT ts_delete('base hidden rebel spaceship strike'::tsvector2, 'spaceship');

SELECT ts_delete('base:7 hidden:6 rebel:1 spaceship:2,33A,34B,35C,36D strike:3'::tsvector2, ARRAY['spaceship','rebel']);
SELECT ts_delete('base:7 hidden:6 rebel:1 spaceship:2,33A,34B,35C,36D strike:3'::tsvector2, ARRAY['spaceships','rebel']);
SELECT ts_delete('base:7 hidden:6 rebel:1 spaceship:2,33A,34B,35C,36D strike:3'::tsvector2, ARRAY['spaceshi','rebel']);
SELECT ts_delete('base:7 hidden:6 rebel:1 spaceship:2,33A,34B,35C,36D strike:3'::tsvector2, ARRAY['spaceship','leya','rebel']);
SELECT ts_delete('base hidden rebel spaceship strike'::tsvector2, ARRAY['spaceship','leya','rebel']);
SELECT ts_delete('base hidden rebel spaceship strike'::tsvector2, ARRAY['spaceship','leya','rebel','rebel']);
SELECT ts_delete('base hidden rebel spaceship strike'::tsvector2, ARRAY['spaceship','leya','rebel', NULL]);

SELECT unnest('base:7 hidden:6 rebel:1 spaceship:2,33A,34B,35C,36D strike:3'::tsvector2);
SELECT unnest('base hidden rebel spaceship strike'::tsvector2);
SELECT * FROM unnest('base:7 hidden:6 rebel:1 spaceship:2,33A,34B,35C,36D strike:3'::tsvector2);
SELECT * FROM unnest('base hidden rebel spaceship strike'::tsvector2);
SELECT lexeme, positions[1] from unnest('base:7 hidden:6 rebel:1 spaceship:2,33A,34B,35C,36D strike:3'::tsvector2);

SELECT tsvector2_to_array('base:7 hidden:6 rebel:1 spaceship:2,33A,34B,35C,36D strike:3'::tsvector2);
SELECT tsvector2_to_array('base hidden rebel spaceship strike'::tsvector2);

SELECT array_to_tsvector2(ARRAY['base','hidden','rebel','spaceship','strike']);
SELECT array_to_tsvector2(ARRAY['base','hidden','rebel','spaceship', NULL]);
-- array_to_tsvector2 must sort and de-dup
SELECT array_to_tsvector2(ARRAY['foo','bar','baz','bar']);

SELECT setweight('w:12B w:13* w:12,5,6 a:1,3* a:3 w asd:1dc asd zxc:81,567,222A'::tsvector2, 'c');
SELECT setweight('a:1,3A asd:1C w:5,6,12B,13A zxc:81,222A,567'::tsvector2, 'c');
SELECT setweight('a:1,3A asd:1C w:5,6,12B,13A zxc:81,222A,567'::tsvector2, 'c', '{a}');
SELECT setweight('a:1,3A asd:1C w:5,6,12B,13A zxc:81,222A,567'::tsvector2, 'c', '{a}');
SELECT setweight('a:1,3A asd:1C w:5,6,12B,13A zxc:81,222A,567'::tsvector2, 'c', '{a,zxc}');
SELECT setweight('a asd w:5,6,12B,13A zxc'::tsvector2, 'c', '{a,zxc}');
SELECT setweight('a asd w:5,6,12B,13A zxc'::tsvector2, 'c', ARRAY['a', 'zxc', NULL]);

SELECT ts_filter('base:7A empir:17 evil:15 first:11 galact:16 hidden:6A rebel:1A spaceship:2A strike:3A victori:12 won:9'::tsvector2, '{a}');
SELECT ts_filter('base hidden rebel spaceship strike'::tsvector2, '{a}');
SELECT ts_filter('base hidden rebel spaceship strike'::tsvector2, '{a,b,NULL}');

CREATE TABLE test_tsvector2 (a tsvector2);
INSERT INTO test_tsvector2 VALUES ('DFG:1A,2B,6C,10 FGH');
SELECT * FROM tsvector2_stat('SELECT a FROM test_tsvector2') ORDER BY ndoc DESC, nentry DESC, word LIMIT 10;
SELECT * FROM tsvector2_stat('SELECT a FROM test_tsvector2', 'AB') ORDER BY ndoc DESC, nentry DESC, word;

drop extension tsvector2 cascade;
