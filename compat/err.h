#ifndef MAILZ_COMPAT_ERR_H
#define MAILZ_COMPAT_ERR_H
#include_next <err.h>
#include <stdarg.h>

void errc(int, int, const char *, ...);
void verrc(int, int, const char *, va_list);
#endif /* MAILZ_COMPAT_ERR_H */
