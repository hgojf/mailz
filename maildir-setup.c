#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "maildir-setup.h"

/*
 * takes a maildir directory and moves all letters in the 'new' directory
 * to the 'cur' directory, appending :2, if it is not already present
 * the single argument is the root of the maildir
 */
int
main(int argc, char *argv[])
{
	DIR *new;
	struct dirent *de;
	ssize_t nw;
	int curfd, mainfd, newfd, rv, save_errno;

	if (argc != 2) {
		save_errno = 0;
		rv = MAILDIR_SETUP_USAGE;
		goto fail;
	}

	/* XXX: unveil 'cur' as 'c' and 'new' as 'r' */
	if (unveil(argv[1], "rc") == -1) {
		save_errno = errno;
		rv = MAILDIR_SETUP_UNVEIL;
		goto fail;
	}
	if (pledge("stdio rpath cpath", NULL) == -1) {
		save_errno = errno;
		rv = MAILDIR_SETUP_PLEDGE;
		goto fail;
	}

	if ((mainfd = open(argv[1], O_RDONLY | O_DIRECTORY)) == -1) {
		save_errno = errno;
		rv = MAILDIR_SETUP_OPENMAIN;
		goto fail;
	}

	if ((curfd = openat(mainfd, "cur", O_RDONLY | O_DIRECTORY)) == -1) {
		save_errno = errno;
		rv = MAILDIR_SETUP_OPENCUR;
		goto main;
	}

	if ((newfd = openat(mainfd, "new", O_RDONLY | O_DIRECTORY)) == -1) {
		save_errno = errno;
		rv = MAILDIR_SETUP_OPENNEW;
		goto cur;
	}
	if ((new = fdopendir(newfd)) == NULL) {
		save_errno = errno;
		rv = MAILDIR_SETUP_FDOPENNEW;
		(void) close(newfd);
		goto cur;
	}

	for (;;) {
		char path[NAME_MAX], *pathp;

		errno = 0;
		if ((de = readdir(new)) == NULL) {
			if (errno != 0) {
				save_errno = errno;
				rv = MAILDIR_SETUP_READDIR;
				goto new;
			}
			/* else EOF */
			break;
		}

		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;

		/* no info string, have to append it ourselves */
		if (strstr(de->d_name, ":2,") == NULL) {
			int n;

			n = snprintf(path, sizeof(path), "%s:2,", de->d_name);
			if (n < 0) {
				save_errno = errno;
				rv = MAILDIR_SETUP_SNPRINTF;
				goto new;
			}
			if (n >= sizeof(path)) {
				save_errno = 0;
				rv = MAILDIR_SETUP_TOOLONG;
				goto new;
			}

			pathp = path;
		}
		else {
			/* info string already there, we can just reuse the d_name */
			pathp = de->d_name;
		}

		if (renameat(newfd, de->d_name, curfd, pathp) == -1) {
			save_errno = errno;
			rv = MAILDIR_SETUP_RENAME;
			goto new;
		}
	}

	save_errno = 0;
	rv = MAILDIR_SETUP_OK;
	new:
	if (closedir(new) == -1 && rv == MAILDIR_SETUP_OK) {
		save_errno = errno;
		rv = MAILDIR_SETUP_CLOSE;
	}
	cur:
	if (close(curfd) == -1 && rv == MAILDIR_SETUP_OK) {
		save_errno = errno;
		rv = MAILDIR_SETUP_CLOSE;
	}
	main:
	if (close(mainfd) == -1 && rv == MAILDIR_SETUP_OK) {
		save_errno = errno;
		rv = MAILDIR_SETUP_CLOSE;
	}
	fail:
	nw = write(STDOUT_FILENO, &save_errno, sizeof(save_errno));
	if (rv == MAILDIR_SETUP_OK) {
		if (nw == -1) {
			rv = MAILDIR_SETUP_WRITE;
		}
		else if (nw != sizeof(save_errno))
			rv = MAILDIR_SETUP_SWRITE;
	}
	return rv;
}
