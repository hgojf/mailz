#ifndef MAILZ_COMPAT_FCNTL_H
#define MAILZ_COMPAT_FCNTL_H
#include_next <fcntl.h>

#ifdef __linux__
/*
 * Linux puts the flock(2) declarations in sys/file.h,
 * while everyone else uses fcntl.h
 */
#include <sys/file.h>
#endif /* __linux__ */
#endif /* MAILZ_COMPAT_FCNTL_H */
