CREATE EXTENSION tsvector2;

SELECT 'a b:89  ca:23A,64b d:34c'::tsvector2 @@ 'd:AC & ca' as "true";
SELECT 'a b:89  ca:23A,64b d:34c'::tsvector2 @@ 'd:AC & ca' as "true";
SELECT 'a b:89  ca:23A,64b d:34c'::tsvector2 @@ 'd:AC & ca:B' as "true";
SELECT 'a b:89  ca:23A,64b d:34c'::tsvector2 @@ 'd:AC & ca:A' as "true";
SELECT 'a b:89  ca:23A,64b d:34c'::tsvector2 @@ 'd:AC & ca:C' as "false";
SELECT 'a b:89  ca:23A,64b d:34c'::tsvector2 @@ 'd:AC & ca:CB' as "true";
SELECT 'a b:89  ca:23A,64b d:34c'::tsvector2 @@ 'd:AC & c:*C' as "false";
SELECT 'a b:89  ca:23A,64b d:34c'::tsvector2 @@ 'd:AC & c:*CB' as "true";
SELECT 'a b:89  ca:23A,64b cb:80c d:34c'::tsvector2 @@ 'd:AC & c:*C' as "true";
SELECT 'a b:89  ca:23A,64c cb:80b d:34c'::tsvector2 @@ 'd:AC & c:*C' as "true";
SELECT 'a b:89  ca:23A,64c cb:80b d:34c'::tsvector2 @@ 'd:AC & c:*B' as "true";

-- if text or tsquery first
SELECT 'd:AC & ca'::tsquery @@ 'a b:89  ca:23A,64b d:34c'::tsvector2 as "true";
SELECT 'd:AC & ca' @@ 'a b:89  ca:23A,64b d:34c'::tsvector2 as "true";

SELECT 'supernova'::tsvector2 @@ 'super'::tsquery AS "false";
SELECT 'supeanova supernova'::tsvector2 @@ 'super'::tsquery AS "false";
SELECT 'supeznova supernova'::tsvector2 @@ 'super'::tsquery AS "false";
SELECT 'supernova'::tsvector2 @@ 'super:*'::tsquery AS "true";
SELECT 'supeanova supernova'::tsvector2 @@ 'super:*'::tsquery AS "true";
SELECT 'supeznova supernova'::tsvector2 @@ 'super:*'::tsquery AS "true";

--phrase search
SELECT to_tsvector2('simple', '1 2 3 1') @@ '1 <-> 2' AS "true";
SELECT to_tsvector2('simple', '1 2 3 1') @@ '1 <2> 2' AS "false";
SELECT to_tsvector2('simple', '1 2 3 1') @@ '1 <-> 3' AS "false";
SELECT to_tsvector2('simple', '1 2 3 1') @@ '1 <2> 3' AS "true";
SELECT to_tsvector2('simple', '1 2 1 2') @@ '1 <3> 2' AS "true";

SELECT to_tsvector2('simple', '1 2 11 3') @@ '1 <-> 3' AS "false";
SELECT to_tsvector2('simple', '1 2 11 3') @@ '1:* <-> 3' AS "true";

SELECT to_tsvector2('simple', '1 2 3 4') @@ '1 <-> 2 <-> 3' AS "true";
SELECT to_tsvector2('simple', '1 2 3 4') @@ '(1 <-> 2) <-> 3' AS "true";
SELECT to_tsvector2('simple', '1 2 3 4') @@ '1 <-> (2 <-> 3)' AS "true";
SELECT to_tsvector2('simple', '1 2 3 4') @@ '1 <2> (2 <-> 3)' AS "false";
SELECT to_tsvector2('simple', '1 2 1 2 3 4') @@ '(1 <-> 2) <-> 3' AS "true";
SELECT to_tsvector2('simple', '1 2 1 2 3 4') @@ '1 <-> 2 <-> 3' AS "true";
-- without position data, phrase search does not match
SELECT strip(to_tsvector2('simple', '1 2 3 4')) @@ '1 <-> 2 <-> 3' AS "false";

select to_tsvector2('simple', 'q x q y') @@ 'q <-> (x & y)' AS "false";
select to_tsvector2('simple', 'q x') @@ 'q <-> (x | y <-> z)' AS "true";
select to_tsvector2('simple', 'q y') @@ 'q <-> (x | y <-> z)' AS "false";
select to_tsvector2('simple', 'q y z') @@ 'q <-> (x | y <-> z)' AS "true";
select to_tsvector2('simple', 'q y x') @@ 'q <-> (x | y <-> z)' AS "false";
select to_tsvector2('simple', 'q x y') @@ 'q <-> (x | y <-> z)' AS "true";
select to_tsvector2('simple', 'q x') @@ '(x | y <-> z) <-> q' AS "false";
select to_tsvector2('simple', 'x q') @@ '(x | y <-> z) <-> q' AS "true";
select to_tsvector2('simple', 'x y q') @@ '(x | y <-> z) <-> q' AS "false";
select to_tsvector2('simple', 'x y z') @@ '(x | y <-> z) <-> q' AS "false";
select to_tsvector2('simple', 'x y z q') @@ '(x | y <-> z) <-> q' AS "true";
select to_tsvector2('simple', 'y z q') @@ '(x | y <-> z) <-> q' AS "true";
select to_tsvector2('simple', 'y y q') @@ '(x | y <-> z) <-> q' AS "false";
select to_tsvector2('simple', 'y y q') @@ '(!x | y <-> z) <-> q' AS "true";
select to_tsvector2('simple', 'x y q') @@ '(!x | y <-> z) <-> q' AS "true";
select to_tsvector2('simple', 'y y q') @@ '(x | y <-> !z) <-> q' AS "true";
select to_tsvector2('simple', 'x q') @@ '(x | y <-> !z) <-> q' AS "true";
select to_tsvector2('simple', 'x q') @@ '(!x | y <-> z) <-> q' AS "false";
select to_tsvector2('simple', 'z q') @@ '(!x | y <-> z) <-> q' AS "true";
select to_tsvector2('simple', 'x y q y') @@ '!x <-> y' AS "true";
select to_tsvector2('simple', 'x y q y') @@ '!foo' AS "true";
select to_tsvector2('simple', '') @@ '!foo' AS "true";

DROP EXTENSION tsvector2;
