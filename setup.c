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
