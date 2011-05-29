/*
 * period.h
 *   Implements the PERIOD data type.
 *
 * Portions Copyright (c) 2007-2008, Jeff Davis <jeff@j-davis.com>
 */

#ifndef __period_h

#define __period_h

#include "postgres.h"
#include "fmgr.h"
#include "libpq/pqformat.h"
#include "utils/timestamp.h"
#include "access/skey.h"
#include "access/gist.h"
#include "access/skey.h"
#include "access/hash.h"
#include "utils/elog.h"
#include "utils/palloc.h"
#include "utils/builtins.h"
#include "utils/inet.h"


#include <string.h>

typedef struct period {
	/*
	 * if this is an empty interval, we put it in the
	 * canonical form: [0,0)
	 */
	/* the first number included in the interval */
	TimestampTz first;
	/* the next value after the last value included in the interval */
	TimestampTz next;
} period;

/* return SQL INTERVAL */
Datum length_period(PG_FUNCTION_ARGS);
Datum period_offset_period_timestamptz(PG_FUNCTION_ARGS);
Datum period_offset_sec_period_timestamptz(PG_FUNCTION_ARGS);

/* return TIMESTAMPTZ */
Datum prior_period(PG_FUNCTION_ARGS);
Datum first_period(PG_FUNCTION_ARGS);
Datum last_period(PG_FUNCTION_ARGS);
Datum next_period(PG_FUNCTION_ARGS);

/* return BOOLEAN */
Datum adjacent_period_period(PG_FUNCTION_ARGS);
Datum contains_period_timestamptz(PG_FUNCTION_ARGS);
Datum contains_period_period(PG_FUNCTION_ARGS);
Datum contained_by_timestamptz_period(PG_FUNCTION_ARGS);
Datum contained_by_period_period(PG_FUNCTION_ARGS);
Datum overlaps_period_period(PG_FUNCTION_ARGS);
Datum overleft_period_period(PG_FUNCTION_ARGS);
Datum overright_period_period(PG_FUNCTION_ARGS);
Datum is_empty_period(PG_FUNCTION_ARGS);
Datum equals_period_period(PG_FUNCTION_ARGS);
Datum nequals_period_period(PG_FUNCTION_ARGS);
Datum before_period_period(PG_FUNCTION_ARGS);
Datum after_period_period(PG_FUNCTION_ARGS);

/* Add operators for <, <=, >=, and > -djg */
Datum lessthan_period_period(PG_FUNCTION_ARGS);
Datum lessthan_period_timestamptz(PG_FUNCTION_ARGS);
Datum lessthan_timestamptz_period(PG_FUNCTION_ARGS);
Datum lessthanequals_period_period(PG_FUNCTION_ARGS);
Datum greaterthan_period_period(PG_FUNCTION_ARGS);
Datum greaterthan_period_timestamptz(PG_FUNCTION_ARGS);
Datum greaterthan_timestamptz_period(PG_FUNCTION_ARGS);
Datum greaterthanequals_period_period(PG_FUNCTION_ARGS);

//merges

/* return period */
Datum empty_period(PG_FUNCTION_ARGS);
Datum minus_period_period(PG_FUNCTION_ARGS);
Datum intersect_period_period(PG_FUNCTION_ARGS);
Datum union_period_period(PG_FUNCTION_ARGS);

/* input/output functions */
Datum period_in(PG_FUNCTION_ARGS);
Datum period_out(PG_FUNCTION_ARGS);
Datum period_oo_timestamptz_timestamptz(PG_FUNCTION_ARGS);
Datum period_oc_timestamptz_timestamptz(PG_FUNCTION_ARGS);
Datum period_co_timestamptz_timestamptz(PG_FUNCTION_ARGS);
Datum period_cc_timestamptz_timestamptz(PG_FUNCTION_ARGS);
Datum period_timestamptz(PG_FUNCTION_ARGS);

/* GiST support functions */
Datum gist_period_consistent(PG_FUNCTION_ARGS);
Datum gist_period_union(PG_FUNCTION_ARGS);
Datum gist_period_compress(PG_FUNCTION_ARGS);
Datum gist_period_decompress(PG_FUNCTION_ARGS);
Datum gist_period_penalty(PG_FUNCTION_ARGS);
Datum gist_period_picksplit(PG_FUNCTION_ARGS);
Datum gist_period_same(PG_FUNCTION_ARGS);

/* btree support functions */
Datum btree_period_compare(PG_FUNCTION_ARGS);
#endif
