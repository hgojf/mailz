#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "config.h"

#ifdef HAVE_FLOCK
int
unlock(int fd)
{
	if (flock(fd, LOCK_UN) == -1)
		return -1;
	return 0;
}

int
lock_interactive(int fd, int ex, const char *what)
{
	int cmd, fv;

	cmd = ex ? LOCK_EX : LOCK_SH;

	if ((fv = flock(fd, cmd | LOCK_NB)) == -1 && errno == EWOULDBLOCK) {
		if (fprintf(stderr, "trying to lock %s... (someone has a lock)\n",
				what) < 0)
			return -1;
		if (flock(fd, cmd) == -1) {
			warn("failed to lock %s", what);
			return -1;
		}
	}
	else if (fv == -1) {
		warn("failed to lock %s", what);
		return -1;
	}

	return 0;
}

#else
int
unlock(int fd)
{
	struct flock lock;

	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_pid = getpid();
	lock.l_type = F_UNLCK;
	lock.l_whence = 0;

	if (fcntl(fd, F_SETLK, &lock) == -1)
		return -1;
	return 0;
}

int
lock_interactive(int fd, int ex, const char *what)
{
	struct flock lock;

	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_pid = getpid();
	lock.l_type = ex ? F_WRLCK : F_RDLCK;
	lock.l_whence = SEEK_SET;

	if (fcntl(fd, F_GETLK, &lock) == -1)
		return -1;
	if (lock.l_type != F_UNLCK) {
		if (fprintf(stderr, "trying to lock %s... (pid %d has a lock)\n",
				what, lock.l_pid) < 0)
		return -1;
	}

	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_pid = getpid();
	lock.l_type = ex ? F_WRLCK : F_RDLCK;
	lock.l_whence = SEEK_SET;
	if (fcntl(fd, F_SETLKW, &lock) == -1) {
		warn("failed to lock %s", what);
		return -1;
	}

	return 0;
}
#endif /* HAVE_FLOCK */
