#ifndef COMPAT_RESOLV_H
#define COMPAT_RESOLV_H

#include_next <resolv.h>

int b64_pton(char const *, unsigned char *, size_t);

#endif /* COMPAT_RESOLV_H */
