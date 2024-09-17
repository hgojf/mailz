#ifndef MAILZ_COMPAT_UNISTD_H
#define MAILZ_COMPAT_UNISTD_H
#include_next <unistd.h>

int getdtablecount(void);

#define pledge(a, b) 0
#define unveil(a, b) 0
#endif /* MAILZ_COMPAT_UNISTD_H */
