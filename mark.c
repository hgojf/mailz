#include <stdio.h>
#include <stdlib.h>

#include "letter.h"
#include "maildir.h"
#include "mark.h"

static int
set_flag(int cur, struct letter *letter, char flag, int on)
{
	char *new;

	if (on)
		new = maildir_setflag(letter->path, flag);
	else
		new = maildir_unsetflag(letter->path, flag);

	if (new == NULL)
		return -1;

	if (new == letter->path)
		return 0; /* nothing to do */

	if (renameat(cur, letter->path, cur, new) == -1) {
		free(new);
		return -1;
	}

	free(letter->path);
	letter->path = new;

	return 0;
}

int
mark_read(int cur, struct letter *letter)
{
	return set_flag(cur, letter, 'S', 1);
}

int
mark_unread(int cur, struct letter *letter)
{
	return set_flag(cur, letter, 'S', 0);
}
