#ifndef COMPAT_ERR_H
#define COMPAT_ERR_H

#include_next <err.h>

void warnc(int, const char *, ...)
	__attribute__((__format__(printf, 2, 3)));

#endif /* COMPAT_ERR_H */
