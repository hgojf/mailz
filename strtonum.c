#include <errno.h>
#include <limits.h>
#include <stdlib.h>

long long
strtonum(const char *nptr, long long minval, long long maxval,
	const char **errstr)
{
	static const char *invalid = "invalid";
	static const char *toobig = "too big";
	static const char *toosmall = "too small";
	long long rv;
	char *ep;

	errno = 0;
	rv = strtoll(nptr, &ep, 10);
	if (rv < minval || (errno == ERANGE && rv == LLONG_MIN))
		*errstr = toosmall;
	else if (rv > maxval || (errno == ERANGE && rv == LLONG_MAX))
		*errstr = toobig;
	else if (*ep != '\0' || *nptr == '\0')
		*errstr = invalid;
	else
		*errstr = NULL;
	return rv;
}
