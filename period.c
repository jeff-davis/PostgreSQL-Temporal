/*
 * period.c
 *   Implements the PERIOD data type.
 *
 * Portions Copyright (c) 2007-2008, Jeff Davis <jeff@j-davis.com>
 */

#include "period.h"

PG_MODULE_MAGIC;

/* only used when HAVE_INT64_TIMESTAMP is not defined */
#define DOUBLE_INF ((double)(1.0/0.0))

/* larger than largest valid period input */
#define MAX_REPR_SIZE 100

/* support functions */
static TimestampTz period_length(period *p);

static period *period_dup(period *src);
static period *period_copy(period *src, period *dst);

static bool period_equals(period *p1, period *p2);
static bool period_is_empty(period *p);
static TimestampTz prior_timestamptz(TimestampTz ts);
static TimestampTz next_timestamptz(TimestampTz ts);
static bool period_adjacent(period *p1, period *p2);
static bool period_contains(period *p1, period *p2);
static bool period_overlaps(period *p1, period *p2);
static bool period_overleft(period *p1, period *p2);
static bool period_overright(period *p1, period *p2);
static bool period_before(period *p1, period *p2);

static period *period_empty_period(period *result);
static period *period_minus(period *p1, period *p2, period *result);
static period *period_intersect(period *p1, period *p2, period *result);
static period *period_union(period *p1, period *p2, period *result, bool greedy);


static bool gist_period_int_consistent(period *p, period *query,
	StrategyNumber strategy);
static bool gist_period_leaf_consistent(period *p, period *query,
	StrategyNumber strategy);

static int period_compare(period *p1, period *p2);
static bool period_lessthan_timestamptz(period *p, TimestampTz ts);	/* djg */
static bool period_greaterthan_timestamptz(period *p, TimestampTz ts); /* djg */

static float period_size_approx(period *p);
static float period_penalty(period *orig, period *new);

static period *period_dup(period *src)
{
	period *dst;
	dst = (period*) palloc(sizeof(period));
	return period_copy(src, dst);
}

static period *period_copy(period *src, period *dst)
{
	dst->first = src->first;
	dst->next = src->next;
	return dst;
}


/*
 * These states are for the parsing of
 * the period input.
 */
#define STATE_INIT 0
#define STATE_STR1 1
#define STATE_STR2 2
#define STATE_DONE 3

PG_FUNCTION_INFO_V1(period_in);
Datum
period_in(PG_FUNCTION_ARGS)
{
	char *str = PG_GETARG_CSTRING(0);
	period *result;
	char str1[MAX_REPR_SIZE];
	char str2[MAX_REPR_SIZE];
	int i, len1=0, len2=0;
	int state=0;
	bool empty=false;
	bool first_inc=false, second_inc=false;
	TimestampTz ts1, ts2;

	result = (period*) palloc(sizeof(period));

	/* parse input so that we have:
	 *  str1 and str2, and whether each should be interpreted as
	 *  inclusive or exclusive, based on '[' or ')', respectively.
	 */
	for(i=0; str[i] && i < MAX_REPR_SIZE - 1; i++) {
		if(state == STATE_INIT) {
			switch(str[i]) {
			case ' ':
				continue;
			case '-':
				empty = true;
				state = STATE_DONE;
				continue;
			case '[':
				state = STATE_STR1;
				first_inc = true;
				continue;
			case '(':
				state = STATE_STR1;
				first_inc = false;
				continue;
			default:
				elog(ERROR,"Invalid period input");
			}
		}
		else if(state == STATE_STR1) {
			if(str[i] == ',') {
				state = STATE_STR2;
				continue;
			}
			str1[len1++] = str[i];
			continue;
		}
		else if(state == STATE_STR2) {
			if(str[i] == ']') {
				second_inc = true;
				state = STATE_DONE;
				break;
			}
			if(str[i] == ')') {
				second_inc = false;
				state = STATE_DONE;
				break;
			}
			str2[len2++] = str[i];
			continue;
		}
		else if(state == STATE_DONE)
			break;
	}
	str1[len1] = '\0';
	str2[len2] = '\0';

	if(state != STATE_DONE) {
		elog(ERROR,"invalid period input: parse error");
	}

	if(empty) {
		result->first = (TimestampTz)0;
		result->next = (TimestampTz)0;
		PG_RETURN_POINTER(result);
	}

	ts1 = DatumGetTimestampTz(DirectFunctionCall3(
		timestamptz_in,CStringGetDatum(str1),
		ObjectIdGetDatum(InvalidOid),Int32GetDatum(-1)));
	ts2 = DatumGetTimestampTz(DirectFunctionCall3(
		timestamptz_in,CStringGetDatum(str2),
		ObjectIdGetDatum(InvalidOid),Int32GetDatum(-1)));

	/* interpret inclusive/exclusive notation */
	if(first_inc)
		result->first = ts1;
	else
		result->first = next_timestamptz(ts1);
	if(second_inc)
		result->next = next_timestamptz(ts2);
	else
		result->next = ts2;

	if(result->first >= result->next)
		elog(ERROR,"invalid period: first > last");

	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(period_out);
Datum
period_out(PG_FUNCTION_ARGS)
{
	period *p = (period*)PG_GETARG_POINTER(0);
	char *result;
	char *ts1,*ts2;

	result = (char*) palloc(MAX_REPR_SIZE);
	if(period_is_empty(p))
		snprintf(result,MAX_REPR_SIZE,"-EMPTY-");
	else {
		ts1 = DatumGetCString(DirectFunctionCall1(timestamptz_out,
			TimestampTzGetDatum(p->first)));
		ts2 = DatumGetCString(DirectFunctionCall1(timestamptz_out,
			TimestampTzGetDatum(p->next)));
		snprintf(result,MAX_REPR_SIZE,"[%s, %s)",ts1,ts2);
	}
	PG_RETURN_CSTRING(result);
}

PG_FUNCTION_INFO_V1(period_oo_timestamptz_timestamptz);
Datum
period_oo_timestamptz_timestamptz(PG_FUNCTION_ARGS)
{
	period *result;
	result = (period*)palloc(sizeof(period));
	result->first = next_timestamptz(PG_GETARG_TIMESTAMPTZ(0));
	result->next = PG_GETARG_TIMESTAMPTZ(1);
	if(result->first > result->next)
		elog(ERROR,"invalid period: first > last");

	if(result->first == result->next) {
		result->first = 0;
		result->next = 0;
	}

	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(period_oc_timestamptz_timestamptz);
Datum
period_oc_timestamptz_timestamptz(PG_FUNCTION_ARGS)
{
	period *result;
	result = (period*)palloc(sizeof(period));
	result->first = next_timestamptz(PG_GETARG_TIMESTAMPTZ(0));
	result->next = next_timestamptz(PG_GETARG_TIMESTAMPTZ(1));
	if(result->first > result->next)
		elog(ERROR,"invalid period: first > last");

	if(result->first == result->next) {
		result->first = 0;
		result->next = 0;
	}

	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(period_co_timestamptz_timestamptz);
Datum
period_co_timestamptz_timestamptz(PG_FUNCTION_ARGS)
{
	period *result;
	result = (period*)palloc(sizeof(period));
	result->first = PG_GETARG_TIMESTAMPTZ(0);
	result->next = PG_GETARG_TIMESTAMPTZ(1);
	if(result->first > result->next)
		elog(ERROR,"invalid period: first > last");

	if(result->first == result->next) {
		result->first = 0;
		result->next = 0;
	}

	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(period_cc_timestamptz_timestamptz);
Datum
period_cc_timestamptz_timestamptz(PG_FUNCTION_ARGS)
{
	period *result;
	result = (period*)palloc(sizeof(period));
	result->first = PG_GETARG_TIMESTAMPTZ(0);
	result->next = next_timestamptz(PG_GETARG_TIMESTAMPTZ(1));
	if(result->first > result->next)
		elog(ERROR,"invalid period: first > last");

	if(result->first == result->next) {
		result->first = 0;
		result->next = 0;
	}

	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(period_timestamptz);
Datum
period_timestamptz(PG_FUNCTION_ARGS)
{
	period *result;
	result = (period*)palloc(sizeof(period));
	result->first = PG_GETARG_TIMESTAMPTZ(0);
	result->next = next_timestamptz(result->first);

	if(result->first == result->next) {
		result->first = 0;
		result->next = 0;
	}

	PG_RETURN_POINTER(result);
}

/*
 * SQL INTERVAL functions
 */

PG_FUNCTION_INFO_V1(length_period);
Datum
length_period(PG_FUNCTION_ARGS)
{
	period *p = (period*)PG_GETARG_POINTER(0);
	Interval *sql_interval = (Interval*)palloc(sizeof(Interval));
	memset(sql_interval, 0, sizeof(Interval));
	sql_interval->time = period_length(p);
	PG_RETURN_INTERVAL_P(sql_interval);
}

PG_FUNCTION_INFO_V1(period_offset_period_timestamptz);
Datum
period_offset_period_timestamptz(PG_FUNCTION_ARGS)
{
	period *p = (period*)PG_GETARG_POINTER(0);
	TimestampTz ts = PG_GETARG_TIMESTAMPTZ(1);
	Interval *sql_interval = (Interval*)palloc(sizeof(Interval));

	memset(sql_interval, 0, sizeof(Interval));

	if (p->first > ts || ts >= p->next)
		elog(ERROR, "input time must be contained in the given period");

	sql_interval->time = ts - p->first;

	PG_RETURN_INTERVAL_P(sql_interval);
}

PG_FUNCTION_INFO_V1(period_offset_sec_period_timestamptz);
Datum
period_offset_sec_period_timestamptz(PG_FUNCTION_ARGS)
{
	period *p = (period*)PG_GETARG_POINTER(0);
	TimestampTz ts = PG_GETARG_TIMESTAMPTZ(1);
	int64 sec;

	if (p->first > ts || ts >= p->next)
		elog(ERROR, "input time must be contained in the given period");

	/* using integer division, so the time will be truncated to the second */
	sec = (ts - p->first) / 1000000;

	PG_RETURN_INT64(sec);
}

/*
 * TimestampTz functions
 */

PG_FUNCTION_INFO_V1(first_period);
Datum
first_period(PG_FUNCTION_ARGS)
{
	period *p = (period*)PG_GETARG_POINTER(0);
	if(period_is_empty(p))
		elog(ERROR,"Interval is empty");
	PG_RETURN_TIMESTAMPTZ(p->first);
}

PG_FUNCTION_INFO_V1(last_period);
Datum
last_period(PG_FUNCTION_ARGS)
{
	period *p = (period*)PG_GETARG_POINTER(0);
	if(period_is_empty(p))
		elog(ERROR,"Interval is empty");
	PG_RETURN_TIMESTAMPTZ(prior_timestamptz(p->next));
}

PG_FUNCTION_INFO_V1(next_period);
Datum
next_period(PG_FUNCTION_ARGS)
{
	period *p = (period*)PG_GETARG_POINTER(0);
	if(period_is_empty(p))
		elog(ERROR,"Interval is empty");
	PG_RETURN_TIMESTAMPTZ(p->next);
}

PG_FUNCTION_INFO_V1(prior_period);
Datum
prior_period(PG_FUNCTION_ARGS)
{
	period *p = (period*)PG_GETARG_POINTER(0);
	if(period_is_empty(p))
		elog(ERROR,"Interval is empty");
	PG_RETURN_TIMESTAMPTZ(prior_timestamptz(p->first));
}

/*
 * BOOLEAN functions
 */

PG_FUNCTION_INFO_V1(equals_period_period);
Datum
equals_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(period_equals(p1,p2));
}

PG_FUNCTION_INFO_V1(nequals_period_period);
Datum
nequals_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(!period_equals(p1,p2));
}

PG_FUNCTION_INFO_V1(lessthan_period_period);
Datum
lessthan_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(period_compare(p1, p2) < 0);
}

PG_FUNCTION_INFO_V1(lessthanequals_period_period);
Datum
lessthanequals_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(period_compare(p1, p2) <= 0);
}


PG_FUNCTION_INFO_V1(greaterthan_period_period);
Datum
greaterthan_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(period_compare(p1, p2) > 0);
}

PG_FUNCTION_INFO_V1(greaterthanequals_period_period);
Datum
greaterthanequals_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(period_compare(p1, p2) >= 0);
}

PG_FUNCTION_INFO_V1(lessthan_period_timestamptz);
Datum
lessthan_period_timestamptz(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	TimestampTz arg = PG_GETARG_TIMESTAMPTZ(1);
	PG_RETURN_BOOL(period_lessthan_timestamptz(p1, arg));
}

PG_FUNCTION_INFO_V1(greaterthan_period_timestamptz);
Datum
greaterthan_period_timestamptz(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	TimestampTz arg = PG_GETARG_TIMESTAMPTZ(1);
	PG_RETURN_BOOL(period_greaterthan_timestamptz(p1, arg));
}

PG_FUNCTION_INFO_V1(lessthan_timestamptz_period);
Datum
lessthan_timestamptz_period(PG_FUNCTION_ARGS)
{
	TimestampTz arg = PG_GETARG_TIMESTAMPTZ(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);
	PG_RETURN_BOOL(period_greaterthan_timestamptz(p2, arg));	/* Implement timestamp < period as period > timestamp instead */
}

PG_FUNCTION_INFO_V1(greaterthan_timestamptz_period);
Datum
greaterthan_timestamptz_period(PG_FUNCTION_ARGS)
{
	TimestampTz arg = PG_GETARG_TIMESTAMPTZ(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);
	PG_RETURN_BOOL(period_lessthan_timestamptz(p2, arg));	/* Implement timestamp > period as period < timestamp instead */
}



PG_FUNCTION_INFO_V1(before_period_period);
Datum
before_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(period_before(p1,p2));
}

PG_FUNCTION_INFO_V1(after_period_period);
Datum
after_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(period_before(p2,p1));
}

PG_FUNCTION_INFO_V1(adjacent_period_period);
Datum
adjacent_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(period_adjacent(p1,p2));
}

PG_FUNCTION_INFO_V1(contains_period_timestamptz);
Datum
contains_period_timestamptz(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	TimestampTz arg = PG_GETARG_TIMESTAMPTZ(1);

	period p2;
	p2.first = arg;
	p2.next = next_timestamptz(arg);

	PG_RETURN_BOOL(period_contains(p1,&p2));
}

PG_FUNCTION_INFO_V1(contains_period_period);
Datum
contains_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(period_contains(p1,p2));
}

PG_FUNCTION_INFO_V1(contained_by_timestamptz_period);
Datum
contained_by_timestamptz_period(PG_FUNCTION_ARGS)
{
	TimestampTz arg = PG_GETARG_TIMESTAMPTZ(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);

	period p1;
	p1.first = arg;
	p1.next = next_timestamptz(arg);

	PG_RETURN_BOOL(period_contains(p2,&p1));
}

PG_FUNCTION_INFO_V1(contained_by_period_period);
Datum contained_by_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(period_contains(p2,p1));
}

PG_FUNCTION_INFO_V1(overlaps_period_period);
Datum
overlaps_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);
	PG_RETURN_BOOL(period_overlaps(p1,p2));
}

PG_FUNCTION_INFO_V1(overleft_period_period);
Datum
overleft_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);
	PG_RETURN_BOOL(period_overleft(p1,p2));
}

PG_FUNCTION_INFO_V1(overright_period_period);
Datum
overright_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);
	PG_RETURN_BOOL(period_overright(p1,p2));
}

PG_FUNCTION_INFO_V1(is_empty_period);
Datum
is_empty_period(PG_FUNCTION_ARGS)
{
	period *p = (period*)PG_GETARG_POINTER(0);
	PG_RETURN_BOOL(period_is_empty(p));
}

/*
 * period functions
 */

PG_FUNCTION_INFO_V1(empty_period);
Datum
empty_period(PG_FUNCTION_ARGS)
{
	PG_RETURN_POINTER(period_empty_period(NULL));
}

PG_FUNCTION_INFO_V1(intersect_period_period);
Datum
intersect_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);
	PG_RETURN_POINTER(period_intersect(p1,p2,NULL));
}


PG_FUNCTION_INFO_V1(union_period_period);
Datum
union_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);
	PG_RETURN_POINTER(period_union(p1,p2,NULL,false));
}

PG_FUNCTION_INFO_V1(minus_period_period);
Datum
minus_period_period(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);
	PG_RETURN_POINTER(period_minus(p1,p2,NULL));
}

/***********************************************
 * GiST Support Functions
 ***********************************************/

#if 1

#define GISTENTRYCOUNT(v) ((v)->n)
#define GISTENTRYVEC(v) ((v)->vector)

#else /* IP4R_PGVER < 8000000 */

typedef bytea GistEntryVector;
#define GISTENTRYCOUNT(v) ( (VARSIZE((v)) - VARHDRSZ) / sizeof(GISTENTRY) )
#define GISTENTRYVEC(v) ((GISTENTRY *) VARDATA(entryvec))

#endif


PG_FUNCTION_INFO_V1(gist_period_consistent);
Datum
gist_period_consistent(PG_FUNCTION_ARGS)
{
	GISTENTRY *entry = (GISTENTRY*) PG_GETARG_POINTER(0);
	period *key = (period*) DatumGetPointer(entry->key);
	StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);
	period query;
	period * period_query;
	TimestampTz t_point_query;

	switch(strategy) {
	case 27: //contains(period,t_point)
	case 28:
		// convert t_point to a period for query
		t_point_query = PG_GETARG_TIMESTAMPTZ(1);
		query.first = t_point_query;
		query.next = next_timestamptz(t_point_query);
		break;
	default:
		period_query = (period*)PG_GETARG_POINTER(1);
		query = *period_query;
	}

	if(GIST_LEAF(entry))
		PG_RETURN_BOOL(gist_period_leaf_consistent(key, &query, strategy));
	else
		PG_RETURN_BOOL(gist_period_int_consistent(key, &query, strategy));

}

PG_FUNCTION_INFO_V1(gist_period_union);
Datum
gist_period_union(PG_FUNCTION_ARGS)
{
	GistEntryVector *entries = (GistEntryVector*) PG_GETARG_POINTER(0);
	int *size = (int*) PG_GETARG_POINTER(1);
	period *result = (period*) palloc(sizeof(period));
	period *tmp_period = NULL;
	int i;

	result->first = result->next = 0;
	for(i = 0; i < entries->n; i++) {
		tmp_period = (period*)DatumGetPointer(entries->vector[i].key);
		if(period_is_empty(result)) {
			result->first = tmp_period->first;
			result->next = tmp_period->next;
		}
		else {
			result->first = (result->first < tmp_period->first) ?
				result->first : tmp_period->first;
			result->next = (result->next > tmp_period->next) ?
				result->next : tmp_period->next;
		}
	}
	*size = sizeof(period);
	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(gist_period_compress);
Datum
gist_period_compress(PG_FUNCTION_ARGS)
{
	PG_RETURN_POINTER(PG_GETARG_POINTER(0));
}

PG_FUNCTION_INFO_V1(gist_period_decompress);
Datum
gist_period_decompress(PG_FUNCTION_ARGS)
{
	PG_RETURN_POINTER(PG_GETARG_POINTER(0));
}

PG_FUNCTION_INFO_V1(gist_period_penalty);
Datum
gist_period_penalty(PG_FUNCTION_ARGS)
{
	period *orig = (period*) DatumGetPointer(((GISTENTRY *) PG_GETARG_POINTER(0))->key);
	period *new = (period*) DatumGetPointer(((GISTENTRY *) PG_GETARG_POINTER(1))->key);
	float	   *penalty = (float *) PG_GETARG_POINTER(2);

	return period_penalty(orig, new);
	PG_RETURN_POINTER(penalty);
}

float period_size_approx(period *p)
{
	/* Divide values by 10ish to avoid overflow in the case of infinity */
	return (p->next/10) - (p->first/10);
}

float period_penalty(period *orig, period *new)
{
	period union_p;
	period_union(orig, new, &union_p, true);
	return period_size_approx(&union_p) - period_size_approx(orig);
}


struct period_gist_sort
{
	period *key;
	OffsetNumber pos;
};

/*
 * This is only used for the picksplit algorithm, so it's OK to use
 * approximates.
 */
static int
period_gist_sort_compare(const void *a, const void *b)
{
	float sa = period_size_approx(((struct period_gist_sort *)a)->key);
	float sb = period_size_approx(((struct period_gist_sort *)b)->key);
	return (sa > sb) ? 1 : ((sa == sb) ? 0 : -1);
}

/*
 * Code was copied from ip4r module:
 *   http://pgfoundry.org/projects/ip4r/
 * Please see that project for details.
 */
PG_FUNCTION_INFO_V1(gist_period_picksplit);
Datum
gist_period_picksplit(PG_FUNCTION_ARGS)
{
	GistEntryVector *entryvec = (GistEntryVector *) PG_GETARG_POINTER(0);
	GIST_SPLITVEC *v = (GIST_SPLITVEC *) PG_GETARG_POINTER(1);
	GISTENTRY *ent = GISTENTRYVEC(entryvec);
	OffsetNumber i;
	int	nbytes;
	OffsetNumber maxoff;
	OffsetNumber *listL;
	OffsetNumber *listR;
	bool allisequal = true;
	period pageunion;
	period *cur;
	period *unionL;
	period *unionR;
	int posL = 0;
	int posR = 0;

	posL = posR = 0;
	maxoff = GISTENTRYCOUNT(entryvec) - 1;

	cur = (period *) DatumGetPointer(ent[FirstOffsetNumber].key);
	pageunion = *cur;

	/* find MBR */
	for (i = OffsetNumberNext(FirstOffsetNumber); i <= maxoff; i = OffsetNumberNext(i))
	{
		cur = (period *) DatumGetPointer(ent[i].key);
		if (allisequal == true
			&& (pageunion.first != cur->first || pageunion.next != cur->next))
			allisequal = false;

		if (cur->first < pageunion.first)
			pageunion.first = cur->first;
		if (cur->next > pageunion.next)
			pageunion.next = cur->next;
	}

	nbytes = (maxoff + 2) * sizeof(OffsetNumber);
	listL = (OffsetNumber *) palloc(nbytes);
	listR = (OffsetNumber *) palloc(nbytes);
	unionL = (period *) palloc(sizeof(period));
	unionR = (period *) palloc(sizeof(period));
	v->spl_ldatum = PointerGetDatum(unionL);
	v->spl_rdatum = PointerGetDatum(unionR);
	v->spl_left = listL;
	v->spl_right = listR;

	if (allisequal)
	{
		cur = (period *) DatumGetPointer(ent[OffsetNumberNext(FirstOffsetNumber)].key);
		if (period_equals(cur, &pageunion))
		{
			OffsetNumber split_at = FirstOffsetNumber + (maxoff - FirstOffsetNumber + 1)/2;
			v->spl_nleft = v->spl_nright = 0;
			*unionL = pageunion;
			*unionR = pageunion;

			for (i = FirstOffsetNumber; i < split_at; i = OffsetNumberNext(i))
				v->spl_left[v->spl_nleft++] = i;
			for (; i <= maxoff; i = OffsetNumberNext(i))
				v->spl_right[v->spl_nright++] = i;

			PG_RETURN_POINTER(v);
		}
	}

#define ADDLIST( list_, u_, pos_, num_ ) do { \
	if ( pos_ ) { \
		if ( (u_)->next < (cur)->next ) (u_)->next = (cur)->next; \
		if ( (u_)->first > (cur)->first ) (u_)->first = (cur)->first; \
	} else { \
		*(u_) = *(cur); \
	} \
	(list_)[(pos_)++] = (num_); \
} while(0)

	for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
	{
		cur = (period *) DatumGetPointer(ent[i].key);
		if (cur->first - pageunion.first < pageunion.next - cur->next)
			ADDLIST(listL, unionL, posL, i);
		else
			ADDLIST(listR, unionR, posR, i);
	}

	/* bad disposition, sort by ascending size and resplit */
	if (posR == 0 || posL == 0)
	{
		struct period_gist_sort *arr = (struct period_gist_sort *) palloc(sizeof(struct period_gist_sort) * (maxoff + FirstOffsetNumber));

		for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
		{
			arr[i].key = (period *) DatumGetPointer(ent[i].key);
			arr[i].pos = i;
		}

		qsort(arr + FirstOffsetNumber,
			  maxoff - FirstOffsetNumber + 1,
			  sizeof(struct period_gist_sort),
			  period_gist_sort_compare);

		posL = posR = 0;
		for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
		{
			cur = arr[i].key;
			if (cur->first - pageunion.first < pageunion.next - cur->next)
			ADDLIST(listL, unionL, posL, arr[i].pos);
			else if (cur->first - pageunion.first == pageunion.next - cur->next)
			{
			if (posL > posR)
				ADDLIST(listR, unionR, posR, arr[i].pos);
			else
				ADDLIST(listL, unionL, posL, arr[i].pos);
			}
			else
			ADDLIST(listR, unionR, posR, arr[i].pos);
		}
		pfree(arr);
	}

	v->spl_nleft = posL;
	v->spl_nright = posR;

	PG_RETURN_POINTER(v);

#if 0
	GistEntryVector *entryvec = (GistEntryVector*) PG_GETARG_POINTER(0);
	GIST_SPLITVEC *v = (GIST_SPLITVEC*) PG_GETARG_POINTER(1);
	OffsetNumber i, j;

	period	*datum_alpha, *datum_beta;
	period	datum_l, datum_r;

	period	union_dl, union_dr;

	bool		firsttime;
	float		size_alpha,
				size_beta,
				size_union,
				size_inter;
	float		size_waste,
				waste;
	float		size_l,
				size_r;
	int			nbytes;
	OffsetNumber seed_1 = 1,
				seed_2 = 2;
	OffsetNumber *left,
			   *right;
	OffsetNumber maxoff;

#ifdef GIST_DEBUG
	fprintf(stderr, "picksplit\n");
#endif

	maxoff = entryvec->n - 2;
	nbytes = (maxoff + 2) * sizeof(OffsetNumber);
	v->spl_left = (OffsetNumber *) palloc(nbytes);
	v->spl_right = (OffsetNumber *) palloc(nbytes);

	firsttime = true;
	waste = 0.0;

	for (i = FirstOffsetNumber; i < maxoff; i = OffsetNumberNext(i))
	{
		datum_alpha = (period *) DatumGetPointer(entryvec->vector[i].key);
		for (j = OffsetNumberNext(i); j <= maxoff; j = OffsetNumberNext(j))
		{
			datum_beta = (period *) DatumGetPointer(entryvec->vector[j].key);

			/* compute the wasted space by unioning these guys */
			/* size_waste = size_union - size_inter; */
			size_waste = period_penalty(datum_alpha, datum_beta);

			/*
			 * are these a more promising split that what we've already seen?
			 */
			if (size_waste > waste || firsttime)
			{
				waste = size_waste;
				seed_1 = i;
				seed_2 = j;
				firsttime = false;
			}
		}
	}

	left = v->spl_left;
	v->spl_nleft = 0;
	right = v->spl_right;
	v->spl_nright = 0;

	datum_alpha = (period *) DatumGetPointer(entryvec->vector[seed_1].key);
	period_copy(datum_alpha, &datum_l);
	size_l = period_size_approx(&datum_l);
	datum_beta = (period *) DatumGetPointer(entryvec->vector[seed_2].key);
	period_copy(datum_beta, &datum_r);
	size_r = period_size_approx(&datum_r);

	/*
	 * Now split up the regions between the two seeds.	An important property
	 * of this split algorithm is that the split vector v has the indices of
	 * items to be split in order in its left and right vectors.  We exploit
	 * this property by doing a merge in the code that actually splits the
	 * page.
	 *
	 * For efficiency, we also place the new index tuple in this loop. This is
	 * handled at the very end, when we have placed all the existing tuples
	 * and i == maxoff + 1.
	 */

	maxoff = OffsetNumberNext(maxoff);
	for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
	{
		/*
		 * If we've already decided where to place this item, just put it on
		 * the right list.	Otherwise, we need to figure out which page needs
		 * the least enlargement in order to store the item.
		 */

		if (i == seed_1)
		{
			*left++ = i;
			v->spl_nleft++;
			continue;
		}
		else if (i == seed_2)
		{
			*right++ = i;
			v->spl_nright++;
			continue;
		}

		/* okay, which page needs least enlargement? */
		datum_alpha = (period *) DatumGetPointer(entryvec->vector[i].key);
		period_union(&datum_l, datum_alpha, &union_dl, true);
		period_union(&datum_r, datum_alpha, &union_dr, true);

		/* pick which page to add it to */
		if (period_penalty(datum_alpha, &datum_l) < period_penalty(datum_alpha, &datum_r))
		{
			period_copy(&union_dl, &datum_l);
			*left++ = i;
			v->spl_nleft++;
		}
		else
		{
			period_copy(&union_dr, &datum_r);
			*right++ = i;
			v->spl_nright++;
		}
	}
	*left = *right = FirstOffsetNumber; /* sentinel value, see dosplit() */

	v->spl_ldatum = PointerGetDatum(period_dup(&datum_l));
	v->spl_rdatum = PointerGetDatum(period_dup(&datum_r));

	PG_RETURN_POINTER(v);
#endif
}

PG_FUNCTION_INFO_V1(gist_period_same);
Datum
gist_period_same(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);
	bool *result = (bool*)PG_GETARG_POINTER(2);
	*result = period_equals(p1,p2);
	PG_RETURN_POINTER(result);
}

bool
gist_period_int_consistent(period *key, period *query,
	StrategyNumber strategy)
{
	switch(strategy) {
	case 1:  //strictly before
		return ! period_overright(key,query);
	case 2:  //overleft
		return ! period_before(query,key);
	case 3:  //overlaps
		return period_overlaps(key,query);
	case 4:  //overright
		return ! period_before(key,query);
	case 5:  //strictly after
		return ! period_overleft(key,query);
	case 6:  //same
		return period_contains(key,query);
	case 7:  //contains
	case 17: // alias for contains
	case 27: //contains(period,t_point)
		return period_contains(key,query);
	case 8:  //contained by
	case 18: // alias for contained by
	case 28: //contained by(period,t_point)
		return period_overlaps(key,query);
	default:
		elog(ERROR,"unrecognized strategy number: %d",strategy);
		return false;
	}
}

bool
gist_period_leaf_consistent(period *key, period *query,
	StrategyNumber strategy)
{
	switch(strategy) {
	case 1:  //strictly before
		return period_before(key,query);
	case 2:  //overleft
		return period_overleft(key,query);
	case 3:  //overlaps
		return period_overlaps(key,query);
	case 4:  //overright
		return period_overright(key,query);
	case 5:  //strictly after
		return period_before(query,key);
	case 6:  //same
		return period_equals(key,query);
	case 7:  //contains
	case 17: // alias for contains
	case 27: //contains(period,t_point)
		return period_contains(key,query);
	case 8:  //contained by
	case 18: // alias for contained by
	case 28: //contained by(period,t_point)
		return period_contains(query,key);
	default:
		elog(ERROR,"unrecognized strategy number: %d",strategy);
		return false;
	}
}


/************************************************
 * Support functions
 ************************************************/

TimestampTz
period_length(period *p)
{
	return (p->next - p->first);
}

TimestampTz
prior_timestamptz(TimestampTz ts)
{
	TimestampTz new_ts;
	/* Any modification of infinity or -infinity should return in the original value -djg */
	if(TIMESTAMP_NOT_FINITE(ts)) {
		new_ts = ts;
		return new_ts;
	}

#ifdef HAVE_INT64_TIMESTAMP
	new_ts = ts - 1;
#else
	new_ts = nextafter(ts,-DOUBLE_INF);
#endif
	if(new_ts > ts || TIMESTAMP_NOT_FINITE(new_ts))
		elog(ERROR,"Overflow!");
	return new_ts;
}

TimestampTz
next_timestamptz(TimestampTz ts)
{
	TimestampTz new_ts;
	/* Any modification of infinity or -infinity should return in the original value -djg */
	if(TIMESTAMP_NOT_FINITE(ts)) {
		new_ts = ts;
		return new_ts;
	}

#ifdef HAVE_INT64_TIMESTAMP
	new_ts = ts + 1;
#else
	new_ts = nextafter(ts,DOUBLE_INF);
#endif
	if(new_ts < ts || TIMESTAMP_NOT_FINITE(new_ts))
		elog(ERROR,"Overflow!");
	return new_ts;
}

bool
period_equals(period *p1, period *p2)
{
	return period_compare(p1, p2) == 0;
}

bool
period_lessthan_timestamptz(period *p, TimestampTz ts)
{
	return (p->next < ts);	/* This should work even if ts or p->next is infinity -djg */
}

bool
period_greaterthan_timestamptz(period *p, TimestampTz ts) {
	return (p->first > ts);
}
 


bool
period_is_empty(period *p)
{
	return (p->first == 0) && (p->next == 0);
}

bool
period_adjacent(period *p1, period *p2)
{
	if(period_is_empty(p1) || period_is_empty(p2))
		elog(ERROR,"Interval is empty");
	if(p2->first == p1->next ||
	   p1->first == p2->next )
		return true;
	else
		return false;
}

/*
 * Does p1 contain p2?
 */
bool
period_contains(period *p1, period *p2)
{
	if(period_is_empty(p2))
		return true;
	return (p1->first <= p2->first && p2->next <= p1->next);
}

bool
period_overlaps(period *p1, period *p2)
{
	if(period_is_empty(p1) || period_is_empty(p2))
		return false;

	/*
	 * This will fail if either 'first' field is infinity or any
	 * 'next' field is -infinity. These aren't possible, however --
	 * any period matching those criteria would be empty.
	 */
	return (p1->first < p2->next) && (p2->first < p1->next);
}

bool
period_overleft(period *p1, period *p2)
{
	if(period_is_empty(p1) || period_is_empty(p2))
		elog(ERROR,"Interval is empty");
	return (p1->next <= p2->next);
}

bool
period_overright(period *p1, period *p2)
{
	if(period_is_empty(p1) || period_is_empty(p2))
		elog(ERROR,"Interval is empty");
	return (p1->first >= p2->first);
}

bool
period_before(period *p1, period *p2)
{
	if(period_is_empty(p1) || period_is_empty(p2))
		elog(ERROR,"Interval is empty");
	return (p1->next <= p2->first);
}

period *
period_empty_period(period *result)
{
	if(result == NULL) result = (period*) palloc(sizeof(period));
	result->first = (TimestampTz)0;
	result->next = (TimestampTz)0;
	return result;
}

period *
period_minus(period *p1, period *p2, period *result)
{
	period intersection;

	if(result == NULL) result = (period*) palloc(sizeof(period));

	if(period_is_empty(p1)) {
		return period_empty_period(result);
	}

	if(period_is_empty(p2)) {
		return period_copy(p1, result);
	}

	period_intersect(p1, p2, &intersection);

	if(intersection.first >= intersection.next) {
		// disjoint
		return period_copy(p1, result);
	}
	else if(intersection.first == p1->first) {
		// RHS is before
		result->first = intersection.next;
		result->next = p1->next;
	}
	else if (intersection.next == p1->next) {
		// RHS is after
		result->first = p1->first;
		result->next = intersection.first;
	}
	else
		elog(ERROR,"Can't subtract periods: RHS is contained inside LHS");

	return result;
}

period *
period_intersect(period *p1, period *p2, period *result)
{
	if(result == NULL) result = (period*) palloc(sizeof(period));

	result->first = (p1->first > p2->first) ? p1->first : p2->first;
	result->next  = (p1->next  < p2->next ) ? p1->next  : p2->next;
	if(result->first >= result->next) {
		return period_empty_period(result);
	}

	return result;
}

/*
 * union p1 and p2 and return an interval consisting of
 * all the points in either interval. If the intervals do
 * not overlap and are not adjacent, return an error unless 
 * 'greedy' is true.
 */
period *
period_union(period *p1, period *p2, period *result, bool greedy)
{
	if(!greedy && !period_overlaps(p1,p2) && !period_adjacent(p1,p2))
	   elog(ERROR,"Can only union overlapping, empty, or adjacent intervals");

	if(result == NULL) result = (period*) palloc(sizeof(period));
	if(period_is_empty(p1)) {
		return period_copy(p2, result);
	}
	if(period_is_empty(p2)) {
		return period_copy(p1, result);
	}

	result->first = (p1->first < p2->first) ? p1->first : p2->first;
	result->next  = (p1->next  > p2->next ) ? p1->next  : p2->next;
	Assert(result->first <= result->next);
	return result;
}

/*
 * Return -1, 0 or 1 if the first argument is less than, equal to, or
 * greater than the second
 */
int period_compare(period *p1, period *p2)
{
	// Empty periods are less than anything else.
	if(period_is_empty(p1)) {
		return period_is_empty(p2) ? 0 : -1;
	}
	if(period_is_empty(p2)) return 1;

	if(p1->first == p2->first) {
		if(p1->next == p2->next) {
			return 0;
		}
		return p1->next < p2->next ? -1 : 1;
	}
	return p1->first < p2->first ? -1 : 1;
}

PG_FUNCTION_INFO_V1(btree_period_compare);
Datum
btree_period_compare(PG_FUNCTION_ARGS)
{
	period *p1 = (period*)PG_GETARG_POINTER(0);
	period *p2 = (period*)PG_GETARG_POINTER(1);
	PG_RETURN_INT32(period_compare(p1, p2));
}
