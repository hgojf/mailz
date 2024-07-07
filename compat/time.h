#ifndef MAILZ_COMPAT_TIME_H
#define MAILZ_COMPAT_TIME_H
#include_next <time.h>

char *strptime(const char *, const char *, struct tm *);
#endif /* MAILZ_COMPAT_TIME_H */
