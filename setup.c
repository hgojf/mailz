#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "setup.h"

int
maildir_setup(const char *path, int root, int cur)
{
	DIR *new;
	int newfd, rv;

	rv = -1;

	if ((newfd = openat(root, "new", O_RDONLY | O_CLOEXEC | O_DIRECTORY)) == -1) {
		warn("%s/new", path);
		return -1;
	}
	if ((new = fdopendir(newfd)) == NULL) {
		warn("fdopendir");
		close(newfd);
		return -1;
	}

	for (;;) {
		char name[NAME_MAX], *namep;
		struct dirent *de;

		errno = 0;
		if ((de = readdir(new)) == NULL) {
			if (errno == 0)
				break;
			warn("readdir");
			goto new;
		}

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;

		if (strstr(de->d_name, ":2,") == NULL) {
			int n;

			n = snprintf(name, sizeof(name), "%s:2,", de->d_name);
			if (n < 0) {
				warn("snprintf");
				goto new;
			}
			if ((size_t)n >= sizeof(name)) {
				warnc(ENAMETOOLONG, "%s:2,", de->d_name);
				goto new;
			}
			namep = name;
		}
		else
			namep = de->d_name;

		if (renameat(newfd, de->d_name, cur, namep) == -1) {
			warn("rename %s/new/%s to %s/cur/%s", 
				path, de->d_name, path, namep);
			goto new;
		}
	}

	rv = 0;
	new:
	closedir(new);
	return rv;
}
