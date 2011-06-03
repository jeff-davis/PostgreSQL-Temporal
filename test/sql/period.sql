\set ECHO 0
BEGIN;
\i temporal.sql
\set ECHO all

select '[2009-01-01, 2009-03-01)'::period @> '[2009-02-03, 2009-02-07)'::period;

ROLLBACK;
