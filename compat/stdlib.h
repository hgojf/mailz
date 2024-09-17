#ifndef MAILZ_COMPAT_STDLIB_H
#define MAILZ_COMPAT_STDLIB_H
#include_next <stdlib.h>

long long strtonum(const char *, long long, long long, const char **);
void freezero(void *, size_t);
#endif /* MAILZ_COMPAT_STDLIB_H */
