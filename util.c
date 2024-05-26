#include <limits.h>
#include <stdlib.h>

#include "util.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

size_t
strtosize(const char *nptr, size_t minval, size_t maxval, const char **errstr)
{
	long long lmin, lmax;

	lmin = (long long) min(minval, LLONG_MAX);
	lmax = (long long) min(maxval, LLONG_MAX);
	return (size_t) strtonum(nptr, lmin, lmax, errstr);
}
