#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
	int curfd, mainfd, newfd, rv;

	if (argc != 2)
		errx(1, "invalid usage");

	/* XXX: unveil 'cur' as 'c' and 'new' as 'r' */
	if (unveil(argv[1], "rc") == -1)
		err(1, "unveil %s", argv[1]);
	if (pledge("stdio rpath cpath", NULL) == -1)
		err(1, "pledge");

	if ((mainfd = open(argv[1], O_RDONLY | O_DIRECTORY)) == -1)
		err(1, "%s", argv[1]);

	rv = 1;
	if ((curfd = openat(mainfd, "cur", O_RDONLY | O_DIRECTORY)) == -1) {
		warn("%s/cur", argv[1]);
		goto main;
	}

	if ((newfd = openat(mainfd, "new", O_RDONLY | O_DIRECTORY)) == -1) {
		warn("%s/new", argv[1]);
		goto cur;
	}
	if ((new = fdopendir(newfd)) == NULL) {
		warn("fdopendir");
		(void) close(newfd);
		goto cur;
	}

	for (;;) {
		char path[NAME_MAX], *pathp;

		errno = 0;
		if ((de = readdir(new)) == NULL) {
			if (errno != 0) {
				warn("readdir");
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
				warn("snprintf");
				goto new;
			}
			if (n >= sizeof(path)) {
				warnc(ENAMETOOLONG, "%s:2,", de->d_name);
				goto new;
			}

			pathp = path;
		}
		else {
			/* info string already there, we can just reuse the d_name */
			pathp = de->d_name;
		}

		if (renameat(newfd, de->d_name, curfd, pathp) == -1) {
			warn("rename %s -> %s", de->d_name, pathp);
			goto new;
		}
	}

	rv = 0;
	new:
	if (closedir(new) == -1) {
		warn("closedir");
		rv = 1;
	}
	cur:
	if (close(curfd) == -1) {
		warn("close");
		rv = 1;
	}
	main:
	if (close(mainfd) == -1) {
		warn("close");
		rv = 1;
	}
	return rv;
}
