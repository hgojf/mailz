#ifndef MAILZ_COMPAT_RESOLV_H
#define MAILZ_COMPAT_RESOLV_H
#include_next <resolv.h>

int b64_pton(const char *, unsigned char *, size_t);
#endif /* MAILZ_COMPAT_RESOLV_H */
