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

#include "string-util.h"

int
bounded_strcmp(const char *one, const char *two, size_t two_len)
{
	size_t i;

	for (i = 0; i < two_len && one[i] != '\0'; i++) {
		if ((unsigned char)one[i] > (unsigned char)two[i])
			return 1;
		else if ((unsigned char)one[i] < (unsigned char)two[i])
			return -1;
	}

	if (one[i] == '\0' && i == two_len)
		return 0;
	else if (one[i] == '\0')
		return 1;
	else
		return -1;
}

int
bounded_strcasecmp(const char *one, const char *two, size_t two_len)
{
	size_t i;

	for (i = 0; i < two_len && one[i] != '\0'; i++) {
		int a, b;

		a = toupper((unsigned char)one[i]);
		b = toupper((unsigned char)two[i]);
		if (a > b)
			return 1;
		else if (a < b)
			return -1;
	}

	if (one[i] == '\0' && i == two_len)
		return 0;
	else if (one[i] == '\0')
		return 1;
	else
		return -1;
}
