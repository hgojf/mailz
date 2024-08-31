#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "maildir.h"
#include "setup.h"

int
maildir_setup(int root, int cur)
{
	DIR *new;
	int newfd, rv;

	rv = -1;

	if ((newfd = openat(root, "new", O_RDONLY | O_DIRECTORY)) == -1)
		return -1;
	if ((new = fdopendir(newfd)) == NULL) {
		close(newfd);
		return -1;
	}

	for (;;) {
		struct dirent *de;
		char *namep, name[NAME_MAX];

		errno = 0;
		if ((de = readdir(new)) == NULL) {
			if (errno == 0)
				break;
			goto fail;
		}

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;

		if (strstr(de->d_name, ":2,") == NULL) {
			int n;

			n = snprintf(name, sizeof(name), "%s:2,", de->d_name);
			if (n < 0 || (size_t)n >= sizeof(name))
				goto fail;
			namep = name;
		}
		else
			namep = de->d_name;

		if (renameat(newfd, de->d_name, cur, namep) == -1)
			goto fail;
	}

	rv = 0;
	fail:
	closedir(new);
	return rv;
}
