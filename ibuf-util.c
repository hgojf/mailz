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

#include <sys/queue.h>

#include <imsg.h>
#include <stdlib.h>
#include <string.h>

#include "ibuf-util.h"

int
ibuf_get_delim(struct ibuf *ibuf, char **s, int c)
{
	char *data, *end;
	size_t len, sz;

	data = ibuf_data(ibuf);
	sz = ibuf_size(ibuf);

	if ((end = memchr(data, c, sz)) == NULL)
		return -1;
	len = (size_t)(end - data) + 1;

	if (ibuf_get_string(ibuf, s, len) == -1)
		return -1;

	return 0;
}

int
ibuf_get_string(struct ibuf *ibuf, char **s, size_t n)
{
	char *rv;

	if ((rv = malloc(n + 1)) == NULL)
		return -1;
	if (ibuf_get(ibuf, rv, n) == -1) {
		free(rv);
		return -1;
	}
	rv[n] = '\0';

	*s = rv;

	return 0;
}
