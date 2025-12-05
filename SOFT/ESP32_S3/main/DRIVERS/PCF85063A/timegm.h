#ifndef __TIMEGM__H
#define __TIMEGM__H

#ifndef HAVE_TIMEGM
#include <time.h>

#define EPOCH_YEAR 1970
#define TM_YEAR_BASE 1900

time_t timegm(struct tm *tm);
#endif

#endif
