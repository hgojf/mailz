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

#include "header.h"

int
header_name(FILE *fp, char *buf, size_t bufsz)
{
	size_t n;

	if (bufsz == 0)
		return HEADER_INVALID;

	n = 0;
	for (;;) {
		int ch;

		if ((ch = fgetc(fp)) == EOF)
			return HEADER_INVALID;
		if (ch == ':')
			break;
		if (ch == '\n' && n == 0)
			return HEADER_EOF;

		if (ch < 33 || ch > 126)
			return HEADER_INVALID;

		if (n == bufsz - 1)
			return HEADER_INVALID;
		buf[n++] = ch;
	}

	buf[n] = '\0';
	return HEADER_OK;
}
