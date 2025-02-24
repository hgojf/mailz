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

#include <string.h>

#include "maildir.h"

struct maildir_info {
	const char *flags;
};

static int maildir_get_info(const char *, struct maildir_info *);

static int
maildir_get_info(const char *name, struct maildir_info *info)
{
	const char *flags;

	if ((flags = strchr(name, ':')) == NULL)
		return -1;
	if (strncmp(&flags[1], "2,", 2) != 0)
		return -1;

	info->flags = &flags[3];
	return 0;
}

int
maildir_get_flag(const char *name, int flag)
{
	struct maildir_info info;

	if (maildir_get_info(name, &info) == -1)
		return 0;
	if (strchr(info.flags, flag) != NULL)
		return 1;
	return 0;
}

const char *
maildir_set_flag(const char *name, int flag, char *buf, size_t bufsz)
{
	struct maildir_info info;
	size_t i, j;
	int set;

	if (maildir_get_info(name, &info) == -1)
		return NULL;

	if (bufsz == 0)
		return NULL;

	set = 0;
	for (i = 0, j = 0; name[i] != '\0' && j < bufsz - 1; i++) {
		if (&name[i] >= info.flags) {
			if (name[i] == flag)
				return name;
			if (!set && name[i] > flag) {
				buf[j++] = flag;
				if (j == bufsz - 1)
					return NULL;
				set = 1;
			}
		}

		buf[j++] = name[i];
	}

	if (name[i] != '\0')
		return NULL;

	if (!set) {
		buf[j++] = flag;
		if (j == bufsz - 1)
			return NULL;
	}

	buf[j] = '\0';
	return buf;
}

const char *
maildir_unset_flag(const char *name, int flag, char *buf, size_t bufsz)
{
	struct maildir_info info;
	size_t i, j;

	if (maildir_get_info(name, &info) == -1)
		return name;

	if (bufsz == 0)
		return NULL;

	for (i = 0, j = 0; name[i] != '\0'; i++) {
		if (&name[i] >= info.flags && name[i] == flag)
			continue;
		if (j == bufsz - 1)
			return NULL;
		buf[j++] = name[i];
	}

	if (name[i] != '\0')
		return NULL;
	if (i == j)
		return name;

	buf[j] = '\0';
	return buf;
}
