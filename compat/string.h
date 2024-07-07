#ifndef MAILZ_COMPAT_STRING_H
#define MAILZ_COMPAT_STRING_H
#include_next <string.h>

size_t strlcpy(char *, const char *, size_t);
char *strsep(char **, const char *);
#endif /* MAILZ_COMPAT_STRING_H */
