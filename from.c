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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "from.h"

int
from_parse(char *s, struct from *out)
{
	char *addr, *ne;
	size_t al;

	/* addr */
	if ((addr = strchr(s, '<')) == NULL) {
		out->al = strlen(s);
		out->addr = s;
		out->nl = 0;
		out->name = NULL;

		return 0;
	}

	addr++;
	al = strlen(addr);
	if (addr[al - 1] != '>')
		return -1;

	/* <addr> */
	if (addr - 1 == s) {
		out->al = al - 1;
		out->addr = addr;
		out->nl = 0;
		out->name = NULL;
	}

	ne = addr - 2;
	while (ne > s && isspace((unsigned char)*ne))
		ne--;

	/* whitespace <addr> */
	if (ne == s) {
		out->al = al - 1;
		out->addr = addr;
		out->nl = 0;
		out->name = NULL;
	}

	/* real name <addr> */

	out->al = al - 1;
	out->addr = addr;
	out->nl = (size_t)(ne - s) + 1;
	out->name = s;

	return 0;
}
