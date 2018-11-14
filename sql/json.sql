CREATE EXTENSION tsvector2;

-- jsonb to tsvector
select to_tsvector2('{"a": "aaa bbb ddd ccc", "b": ["eee fff ggg"], "c": {"d": "hhh iii"}}'::jsonb);

-- jsonb to tsvector with config
select to_tsvector2('simple', '{"a": "aaa bbb ddd ccc", "b": ["eee fff ggg"], "c": {"d": "hhh iii"}}'::jsonb);

-- jsonb to tsvector with stop words
select to_tsvector2('english', '{"a": "aaa in bbb ddd ccc", "b": ["the eee fff ggg"], "c": {"d": "hhh. iii"}}'::jsonb);

-- jsonb to tsvector with numeric values
select to_tsvector2('english', '{"a": "aaa in bbb ddd ccc", "b": 123, "c": 456}'::jsonb);

-- jsonb_to_tsvector2
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '"all"');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '"key"');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '"string"');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '"numeric"');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '"boolean"');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '["string", "numeric"]');

select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '"all"');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '"key"');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '"string"');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '"numeric"');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '"boolean"');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '["string", "numeric"]');

-- ts_vector corner cases
select to_tsvector2('""'::jsonb);
select to_tsvector2('{}'::jsonb);
select to_tsvector2('[]'::jsonb);
select to_tsvector2('null'::jsonb);

-- jsonb_to_tsvector2 corner cases
select jsonb_to_tsvector2('""'::jsonb, '"all"');
select jsonb_to_tsvector2('{}'::jsonb, '"all"');
select jsonb_to_tsvector2('[]'::jsonb, '"all"');
select jsonb_to_tsvector2('null'::jsonb, '"all"');

select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '""');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '{}');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '[]');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, 'null');
select jsonb_to_tsvector2('english', '{"a": "aaa in bbb", "b": 123, "c": 456, "d": true, "f": false, "g": null}'::jsonb, '["all", null]');

DROP EXTENSION tsvector2;
