create extension tsvector2;

SELECT '1'::tsvector2;
SELECT '1 '::tsvector2;
SELECT ' 1'::tsvector2;
SELECT ' 1 '::tsvector2;
SELECT '1 2'::tsvector2;
SELECT '''1 2'''::tsvector2;
SELECT E'''1 \\''2'''::tsvector2;
SELECT E'''1 \\''2''3'::tsvector2;
SELECT E'''1 \\''2'' 3'::tsvector2;
SELECT E'''1 \\''2'' '' 3'' 4 '::tsvector2;
SELECT $$'\\as' ab\c ab\\c AB\\\c ab\\\\c$$::tsvector2;
SELECT tsvector2in(tsvector2out($$'\\as' ab\c ab\\c AB\\\c ab\\\\c$$::tsvector2));
SELECT '''w'':4A,3B,2C,1D,5 a:8';
SELECT 'a:3A b:2a'::tsvector2 || 'ba:1234 a:1B';

-- comparison
select 'a:1 b:2'::tsvector2 < 'a:1 b:3'::tsvector2;
select 'a:1 b:2'::tsvector2 <= 'a:1 b:3'::tsvector2;
select 'a:1 b:2'::tsvector2 > 'a:1 b:3'::tsvector2;
select 'a:1 b:2'::tsvector2 >= 'a:1 b:3'::tsvector2;
select 'a:1 b:4'::tsvector2 < 'a:1 b:3'::tsvector2;
select 'a:1 b:4'::tsvector2 <= 'a:1 b:3'::tsvector2;
select 'a:1 b:4'::tsvector2 > 'a:1 b:3'::tsvector2;
select 'a:1 b:4'::tsvector2 >= 'a:1 b:3'::tsvector2;

select 'a:1 b:2'::tsvector2 = 'a:1 b:2'::tsvector2;
select 'a:1 b:2'::tsvector2 <> 'a:1 b:2'::tsvector2;
select 'a:1 b:2'::tsvector2 <= 'a:1 b:2'::tsvector2;
select 'a:1 b:2'::tsvector2 >= 'a:1 b:2'::tsvector2;

select 'a:1 b:2'::tsvector2 = 'a:2 b:4'::tsvector2;
select 'a:1 b:2'::tsvector2 <> 'a:2 b:4'::tsvector2;
select 'a:7 b:9'::tsvector2 = 'a:2 b:4'::tsvector2;
select 'a:7 b:9'::tsvector2 <> 'a:2 b:4'::tsvector2;

drop extension tsvector2;
