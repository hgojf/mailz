/*
 * Copyright (c) 2024 Henry Ford <fordhenry2299@gmail.com>

 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.

 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "config.h"

#ifdef HAVE_FLOCK
static int flock_unlock(int);
static int flock_lock_interactive(int, int, const char *);
#else
static int fcntl_unlock(int);
static int fcntl_lock_interactive(int, int, const char *);
#endif /* HAVE_FLOCK */

int
unlock(int fd)
{
	#ifdef HAVE_FLOCK
	return flock_unlock(fd);
	#else
	return fcntl_unlock(fd);
	#endif /* HAVE_FLOCK */
}

int
lock_interactive(int fd, int ex, const char *what)
{
	#ifdef HAVE_FLOCK
	return flock_lock_interactive(fd, ex, what);
	#else
	return fcntl_lock_interactive(fd, ex, what);
	#endif /* HAVE_FLOCK */
}

#ifdef HAVE_FLOCK
static int
flock_unlock(int fd)
{
	if (flock(fd, LOCK_UN) == -1)
		return -1;
	return 0;
}

static int
flock_lock_interactive(int fd, int ex, const char *what)
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
static int
fcntl_unlock(int fd)
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

static int
fcntl_lock_interactive(int fd, int ex, const char *what)
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
