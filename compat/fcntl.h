#ifndef MAILZ_COMPAT_FCNTL_H
#define MAILZ_COMPAT_FCNTL_H
#include_next <fcntl.h>

#ifdef HAVE_FLOCK_SYS_FILE_H
#include <sys/file.h>
#endif /* HAVE_FLOCK_SYS_FILE_H */
#endif /* MAILZ_COMPAT_FCNTL_H */
