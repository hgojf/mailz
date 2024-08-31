#include <stdlib.h>
#include <string.h>

#include "maildir.h"

struct maildir_info {
	char type;
	const char *flags;
};

static int
maildir_info(const char *path, struct maildir_info *info)
{
	char *type;

	if ((type = strchr(path, ':')) == NULL)
		return -1;
	type++;

	switch (*type) {
	case '1':
	case '2':
		break;
	default:
		return -1;
	}

	if (type[1] != ',')
		return -1;

	info->type = *type;
	info->flags = &type[2];

	return 0;
}

int
maildir_letter_seen(const char *path)
{
	struct maildir_info info;

	if (maildir_info(path, &info) == -1 || info.type != '2')
		return 0;

	return strchr(info.flags, 'S') != NULL;
}

/*
 * Sets a flag on the maildir path given.
 * If an error occurs, returns NULL.
 * If the flag is already set, returns path.
 * Otherwise returns a string allocated by malloc(3).
 * If the flags in path are in alphabetical order, then the returned
 * strings will have flags in alphabetical order.
 */
char *
maildir_setflag(char *path, char flag)
{
	struct maildir_info info;
	const char *fe;
	char *rv, *wp;

	switch (flag) {
	case 'S':
		break;
	default:
		return NULL;
	}

	if (maildir_info(path, &info) == -1)
		return NULL;
	if (info.type != '2')
		return NULL;

	if (strchr(info.flags, flag) != NULL)
		return path;

	if ((rv = malloc(strlen(path) + 2)) == NULL)
		return NULL;

	for (fe = info.flags; *fe != '\0' && *fe < flag; fe++)
		;

	wp = rv;
	for (char *i = path; ;i++) {
		if (i == fe)
			*wp++ = flag;
		*wp++ = *i;

		if (*i == '\0')
			break;
	}

	return rv;
}

/*
 * Unsets a flag on the maildir path given.
 * If an error occurs, returns NULL.
 * If the flag is not set, returns path.
 * Otherwise returns a string allocated by mallloc(3).
 */
char *
maildir_unsetflag(char *path, char flag)
{
	struct maildir_info info;
	char *flagp, *rv;

	switch (flag) {
	case 'S':
		break;
	default:
		return NULL;
	}

	if (maildir_info(path, &info) == -1)
		return NULL;
	if (info.type != '2')
		return path;

	if ((flagp = strchr(info.flags, flag)) == NULL)
		return path;

	if ((rv = strdup(path)) == NULL)
		return NULL;

	/* move everything back, including the NUL */
	memmove(&rv[flagp - path], &flagp[1], strlen(&flagp[1]) + 1);

	return rv;
}
