--
-- temporal--unpackaged--0.7.1.sql
--   SQL script to upgrade the TEMPORAL contrib module to a proper EXTENSION.
--
-- Portions Copyright (c) 2007-2008, Jeff Davis <jeff@j-davis.com>
--

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION temporal FROM unpackaged" to load this file. \quit

ALTER EXTENSION temporal ADD TYPE period;
ALTER EXTENSION temporal ADD FUNCTION period_in(cstring);
ALTER EXTENSION temporal ADD FUNCTION period_out(period);
ALTER EXTENSION temporal ADD FUNCTION length(period);
ALTER EXTENSION temporal ADD FUNCTION period_offset(period,timestamptz);
ALTER EXTENSION temporal ADD FUNCTION period_offset_sec(period,timestamptz);
ALTER EXTENSION temporal ADD FUNCTION first(period);
ALTER EXTENSION temporal ADD FUNCTION last(period);
ALTER EXTENSION temporal ADD FUNCTION prior(period);
ALTER EXTENSION temporal ADD FUNCTION next(period);
ALTER EXTENSION temporal ADD FUNCTION contains(period,timestamptz);
ALTER EXTENSION temporal ADD FUNCTION contains(period,period);
ALTER EXTENSION temporal ADD FUNCTION contained_by(timestamptz,period);
ALTER EXTENSION temporal ADD FUNCTION contained_by(period,period);
ALTER EXTENSION temporal ADD FUNCTION adjacent(period,period);
ALTER EXTENSION temporal ADD FUNCTION overlaps(period,period);
ALTER EXTENSION temporal ADD FUNCTION overleft(period,period);
ALTER EXTENSION temporal ADD FUNCTION overright(period,period);
ALTER EXTENSION temporal ADD FUNCTION is_empty(period);
ALTER EXTENSION temporal ADD FUNCTION equals(period,period);
ALTER EXTENSION temporal ADD FUNCTION nequals(period,period);
ALTER EXTENSION temporal ADD FUNCTION before(period,period);
ALTER EXTENSION temporal ADD FUNCTION after(period,period);
ALTER EXTENSION temporal ADD FUNCTION lessthan(period,period);
ALTER EXTENSION temporal ADD FUNCTION lessthanequals(period,period);
ALTER EXTENSION temporal ADD FUNCTION greaterthan(period,period);
ALTER EXTENSION temporal ADD FUNCTION greaterthanequals(period,period);
ALTER EXTENSION temporal ADD FUNCTION lessthan(period,timestamptz);
ALTER EXTENSION temporal ADD FUNCTION greaterthan(period,timestamptz);
ALTER EXTENSION temporal ADD FUNCTION lessthan(timestamptz,period);
ALTER EXTENSION temporal ADD FUNCTION greaterthan(timestamptz,period);
ALTER EXTENSION temporal ADD FUNCTION empty_period();
ALTER EXTENSION temporal ADD FUNCTION period_intersect(period,period);
ALTER EXTENSION temporal ADD FUNCTION period_union(period,period);
ALTER EXTENSION temporal ADD FUNCTION period(timestamptz);
ALTER EXTENSION temporal ADD FUNCTION period(timestamptz,timestamptz);
ALTER EXTENSION temporal ADD FUNCTION period_oo(timestamptz,timestamptz);
ALTER EXTENSION temporal ADD FUNCTION period_oc(timestamptz,timestamptz);
ALTER EXTENSION temporal ADD FUNCTION period_co(timestamptz,timestamptz);
ALTER EXTENSION temporal ADD FUNCTION period_cc(timestamptz,timestamptz);
ALTER EXTENSION temporal ADD FUNCTION minus(period,period);
ALTER EXTENSION temporal ADD FUNCTION gist_period_consistent(internal,period,int4);
ALTER EXTENSION temporal ADD FUNCTION gist_period_union(internal,internal);
ALTER EXTENSION temporal ADD FUNCTION gist_period_compress(internal);
ALTER EXTENSION temporal ADD FUNCTION gist_period_decompress(internal);
ALTER EXTENSION temporal ADD FUNCTION gist_period_penalty(internal,internal,internal);
ALTER EXTENSION temporal ADD FUNCTION gist_period_picksplit(internal,internal);
ALTER EXTENSION temporal ADD FUNCTION gist_period_same(period,period,internal);
ALTER EXTENSION temporal ADD FUNCTION btree_period_compare(period,period);
ALTER EXTENSION temporal ADD OPERATOR =(period,period);
ALTER EXTENSION temporal ADD OPERATOR !=(period,period);
ALTER EXTENSION temporal ADD OPERATOR -(period,period);
ALTER EXTENSION temporal ADD OPERATOR +(period,period);
ALTER EXTENSION temporal ADD OPERATOR @>(period,period);
ALTER EXTENSION temporal ADD OPERATOR @>(period,timestamptz);
ALTER EXTENSION temporal ADD OPERATOR <@(period,period);
ALTER EXTENSION temporal ADD OPERATOR <@(timestamptz,period);
ALTER EXTENSION temporal ADD OPERATOR ~(period,period);
ALTER EXTENSION temporal ADD OPERATOR ~(period,timestamptz);
ALTER EXTENSION temporal ADD OPERATOR @(period,period);
ALTER EXTENSION temporal ADD OPERATOR @(timestamptz,period);
ALTER EXTENSION temporal ADD OPERATOR &&(period,period);
ALTER EXTENSION temporal ADD OPERATOR <<(period,period);
ALTER EXTENSION temporal ADD OPERATOR >>(period,period);
ALTER EXTENSION temporal ADD OPERATOR &<(period,period);
ALTER EXTENSION temporal ADD OPERATOR &>(period,period);
ALTER EXTENSION temporal ADD OPERATOR <(period,period);
ALTER EXTENSION temporal ADD OPERATOR <=(period,period);
ALTER EXTENSION temporal ADD OPERATOR <(period,timestamptz);
ALTER EXTENSION temporal ADD OPERATOR <(timestamptz,period);
ALTER EXTENSION temporal ADD OPERATOR >(period,period);
ALTER EXTENSION temporal ADD OPERATOR >=(period,period);
ALTER EXTENSION temporal ADD OPERATOR >(period,timestamptz);
ALTER EXTENSION temporal ADD OPERATOR >(timestamptz,period);
ALTER EXTENSION temporal ADD OPERATOR CLASS gist_period_ops USING gist;
ALTER EXTENSION temporal ADD OPERATOR CLASS btree_period_ops USING btree;
