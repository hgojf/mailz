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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

long long
strtonum(const char *nptr, long long minval, long long maxval,
	const char **errstr)
{
	static const char *invalid = "invalid";
	static const char *toobig = "too big";
	static const char *toosmall = "too small";
	long long rv;
	char *ep;

	errno = 0;
	rv = strtoll(nptr, &ep, 10);
	if (rv < minval || (errno == ERANGE && rv == LLONG_MIN))
		*errstr = toosmall;
	else if (rv > maxval || (errno == ERANGE && rv == LLONG_MAX))
		*errstr = toobig;
	else if (*ep != '\0' || *nptr == '\0')
		*errstr = invalid;
	else
		*errstr = NULL;
	return rv;
}
