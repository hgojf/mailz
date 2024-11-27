#include <err.h>
#include <stdarg.h>
#include <unistd.h>

#include "_err.h"

void
_err(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarn(fmt, ap);
	_exit(eval);
	/* NOTREACHED */
	va_end(ap);
}
