CREATE EXTENSION tsvector2;

--ranking
SELECT ts_rank(' a:1 s:2C d g'::tsvector2, 'a | s');
SELECT ts_rank(' a:1 sa:2C d g'::tsvector2, 'a | s');
SELECT ts_rank(' a:1 sa:2C d g'::tsvector2, 'a | s:*');
SELECT ts_rank(' a:1 sa:2C d g'::tsvector2, 'a | sa:*');
SELECT ts_rank(' a:1 s:2B d g'::tsvector2, 'a | s');
SELECT ts_rank(' a:1 s:2 d g'::tsvector2, 'a | s');
SELECT ts_rank(' a:1 s:2C d g'::tsvector2, 'a & s');
SELECT ts_rank(' a:1 s:2B d g'::tsvector2, 'a & s');
SELECT ts_rank(' a:1 s:2 d g'::tsvector2, 'a & s');

SELECT ts_rank_cd(' a:1 s:2C d g'::tsvector2, 'a | s');
SELECT ts_rank_cd(' a:1 sa:2C d g'::tsvector2, 'a | s');
SELECT ts_rank_cd(' a:1 sa:2C d g'::tsvector2, 'a | s:*');
SELECT ts_rank_cd(' a:1 sa:2C d g'::tsvector2, 'a | sa:*');
SELECT ts_rank_cd(' a:1 sa:3C sab:2c d g'::tsvector2, 'a | sa:*');
SELECT ts_rank_cd(' a:1 s:2B d g'::tsvector2, 'a | s');
SELECT ts_rank_cd(' a:1 s:2 d g'::tsvector2, 'a | s');
SELECT ts_rank_cd(' a:1 s:2C d g'::tsvector2, 'a & s');
SELECT ts_rank_cd(' a:1 s:2B d g'::tsvector2, 'a & s');
SELECT ts_rank_cd(' a:1 s:2 d g'::tsvector2, 'a & s');

SELECT ts_rank_cd(' a:1 s:2A d g'::tsvector2, 'a <-> s');
SELECT ts_rank_cd(' a:1 s:2C d g'::tsvector2, 'a <-> s');
SELECT ts_rank_cd(' a:1 s:2 d g'::tsvector2, 'a <-> s');
SELECT ts_rank_cd(' a:1 s:2 d:2A g'::tsvector2, 'a <-> s');
SELECT ts_rank_cd(' a:1 s:2,3A d:2A g'::tsvector2, 'a <2> s:A');
SELECT ts_rank_cd(' a:1 b:2 s:3A d:2A g'::tsvector2, 'a <2> s:A');
SELECT ts_rank_cd(' a:1 sa:2D sb:2A g'::tsvector2, 'a <-> s:*');
SELECT ts_rank_cd(' a:1 sa:2A sb:2D g'::tsvector2, 'a <-> s:*');
SELECT ts_rank_cd(' a:1 sa:2A sb:2D g'::tsvector2, 'a <-> s:* <-> sa:A');
SELECT ts_rank_cd(' a:1 sa:2A sb:2D g'::tsvector2, 'a <-> s:* <-> sa:B');

SELECT ts_rank_cd(to_tsvector2('english', '
Day after day, day after day,
  We stuck, nor breath nor motion,
As idle as a painted Ship
  Upon a painted Ocean.
Water, water, every where
  And all the boards did shrink;
Water, water, every where,
  Nor any drop to drink.
S. T. Coleridge (1772-1834)
'), to_tsquery('english', 'paint&water'));

SELECT ts_rank_cd(to_tsvector2('english', '
Day after day, day after day,
  We stuck, nor breath nor motion,
As idle as a painted Ship
  Upon a painted Ocean.
Water, water, every where
  And all the boards did shrink;
Water, water, every where,
  Nor any drop to drink.
S. T. Coleridge (1772-1834)
'), to_tsquery('english', 'breath&motion&water'));

SELECT ts_rank_cd(to_tsvector2('english', '
Day after day, day after day,
  We stuck, nor breath nor motion,
As idle as a painted Ship
  Upon a painted Ocean.
Water, water, every where
  And all the boards did shrink;
Water, water, every where,
  Nor any drop to drink.
S. T. Coleridge (1772-1834)
'), to_tsquery('english', 'ocean'));

SELECT ts_rank_cd(to_tsvector2('english', '
Day after day, day after day,
  We stuck, nor breath nor motion,
As idle as a painted Ship
  Upon a painted Ocean.
Water, water, every where
  And all the boards did shrink;
Water, water, every where,
  Nor any drop to drink.
S. T. Coleridge (1772-1834)
'), to_tsquery('english', 'painted <-> Ship'));

SELECT ts_rank_cd(strip(to_tsvector2('both stripped')),
                  to_tsquery('both & stripped'));

SELECT ts_rank_cd(to_tsvector2('unstripped') || strip(to_tsvector2('stripped')),
                  to_tsquery('unstripped & stripped'));

DROP EXTENSION tsvector2;
