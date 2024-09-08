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
