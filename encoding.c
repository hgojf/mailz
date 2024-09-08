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

#include <netinet/in.h> /* for resolv.h */

#include <assert.h>
#include <ctype.h>
#include <resolv.h> /* for b64_ntop */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "encoding.h"
#include "util.h"

static int base64(FILE *, struct b64_decode *);
static int hexdigcaps(int);
static int quoted_printable(FILE *);

int
encoding_getbyte(FILE *fp, struct encoding *encoding)
{
	int c;

	switch (encoding->type) {
	case ENCODING_7BIT:
	case ENCODING_8BIT:
	case ENCODING_BINARY:
		if ((c = fgetc(fp)) == EOF)
			return ENCODING_EOF;

		if (encoding->type == ENCODING_7BIT && c > 127)
			return ENCODING_ERR;

		return c;
	case ENCODING_QUOTED_PRINTABLE:
		return quoted_printable(fp);
	case ENCODING_BASE64:
		return base64(fp, &encoding->val.base64);
	}

	/* invalid encoding type */
	abort();
}

int
encoding_set(struct encoding *encoding, const char *type)
{
	struct {
		const char *ident;
		enum encoding_type type;
	} encodings[] = {
		{ "7bit", ENCODING_7BIT },
		{ "8bit", ENCODING_8BIT },
		{ "base64", ENCODING_BASE64 },
		{ "binary", ENCODING_BINARY },
		{ "quoted-printable", ENCODING_QUOTED_PRINTABLE },
	};

	for (size_t i = 0; i < nitems(encodings); i++) {
		if (!strcasecmp(encodings[i].ident, type)) {
			encoding_set_type(encoding, encodings[i].type);
			return 0;
		}
	}

	/* unknown type */

	return -1;
}

/* 
 * This function exists to provide an infallible way
 * to set a default encoding that will never leave the encoding
 * uninitialized.
 */
void
encoding_set_type(struct encoding *encoding, enum encoding_type type)
{
	memset(&encoding->val, 0, sizeof(encoding->val));
	encoding->type = type;
}

static int
hexdigcaps(int c)
{
	if (isdigit(c))
		return c - '0';
	else if (isupper(c))
		return c - 'A' + 10;

	return -1;
}

static int
base64(FILE *fp, struct b64_decode *b64)
{
	char buf[5], obuf[3];
	int ni;

	if (b64->i != b64->end)
		return b64->buf[b64->i++];

	for (int n = 0; n < 4;) {
		int c;

		if ((c = fgetc(fp)) == EOF) {
			if (n == 0)
				return ENCODING_EOF;
			return ENCODING_ERR;
		}

		/* other invalid characters will be caught in b64_pton */
		if (c == '\0')
			return ENCODING_ERR;

		if (isspace(c))
			continue;
		buf[n++] = c;
	}

	buf[4] = '\0';

	if ((ni = b64_pton(buf, obuf, sizeof(obuf))) == -1)
		return ENCODING_ERR;

	/* 
	 * ni will always be >= 1 because we dont allow NUL bytes in 
	 * the input, and because b64_pton will not allow a string 
	 * consisting of only padding.
	 */
	memcpy(b64->buf, &obuf[1], ni - 1);
	b64->i = 0;
	b64->end = ni - 1;

	return obuf[0];
}

static int 
quoted_printable(FILE *fp)
{
	int eq, hi, lo;

	again:
	if ((eq = fgetc(fp)) == EOF)
		return ENCODING_EOF;
	if (eq != '=')
		return eq;

	if ((hi = fgetc(fp)) == EOF)
		return ENCODING_ERR;

	if (hi == '\n') /* soft break */
		goto again;

	if ((lo = fgetc(fp)) == EOF)
		return ENCODING_ERR;

	if ((hi = hexdigcaps(hi)) == -1 || (lo = hexdigcaps(lo)) == -1)
		return ENCODING_ERR;

	return (hi << 4) | lo;
}
