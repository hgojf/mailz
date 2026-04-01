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

#include <resolv.h> /* for b64_pton */
#include <stdio.h>
#include <string.h>

#include "encoding.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

static int encoding_getc_base64(struct encoding_base64 *, FILE *);
static int encoding_getc_qp(FILE *);
static int encoding_getc_raw(FILE *, int, int);
static int hexdigcaps(int);

static const struct {
	const char *ident;
	enum encoding_type type;
} encodings[] = {
	{ "7bit",		ENCODING_7BIT },
	{ "8bit",		ENCODING_8BIT },
	{ "base64",		ENCODING_BASE64 },
	{ "binary",		ENCODING_BINARY },
	{ "quoted-printable",	ENCODING_QP },
};

int
encoding_from_name(const char *name)
{
	size_t i;

	for (i = 0; i < nitems(encodings); i++) {
		if (!strcasecmp(name, encodings[i].ident))
			return i;
	}

	return ENCODING_UNKNOWN;
}

void
encoding_from_type(struct encoding *ep, enum encoding_type type)
{
	switch (type) {
	case ENCODING_7BIT:
	case ENCODING_8BIT:
	case ENCODING_BINARY:
	case ENCODING_QP:
		break;
	case ENCODING_BASE64:
		memset(&ep->state.base64, 0, sizeof(ep->state.base64));
		break;
	}

	ep->type = type;
}

int
encoding_getc(struct encoding *ep, FILE *fp)
{
	switch (ep->type) {
	case ENCODING_7BIT:
		return encoding_getc_raw(fp, 0, 0);
	case ENCODING_8BIT:
		return encoding_getc_raw(fp, 1, 0);
	case ENCODING_BASE64:
		return encoding_getc_base64(&ep->state.base64, fp);
	case ENCODING_BINARY:
		return encoding_getc_raw(fp, 1, 1);
	case ENCODING_QP:
		return encoding_getc_qp(fp);
	}

	return ENCODING_ERR;
}

static int
encoding_getc_base64(struct encoding_base64 *base64, FILE *fp)
{
	char buf[5];
	unsigned char obuf[3];
	int i, n;

	if (base64->start != base64->end)
		return base64->buf[base64->start++];

	for (i = 0; i < 4;) {
		int ch;

		if ((ch = fgetc(fp)) == EOF) {
			if (i == 0)
				return ENCODING_EOF;
			return ENCODING_ERR;
		}

		if (ch == '\0')
			return ENCODING_ERR;

		if (ch == '\n')
			continue;
		buf[i++] = ch;
	}

	buf[4] = '\0';

	if ((n = b64_pton(buf, obuf, sizeof(obuf))) == -1)
		return ENCODING_ERR;

	memcpy(base64->buf, &obuf[1], n - 1);
	base64->start = 0;
	base64->end = n - 1;
	return obuf[0];
}

static int
encoding_getc_qp(FILE *fp)
{
	for (;;) {
		int ch, hi, lo;

		if ((ch = fgetc(fp)) == EOF)
			return ENCODING_EOF;

		if (ch != '=') {
			switch (ch) {
			case ' ':
			case '\t':
			case '\n':
				break;
			default:
				if (ch < 33 || ch > 126)
					return ENCODING_ERR;
			}
			return ch;
		}

		if ((hi = fgetc(fp)) == EOF)
			return ENCODING_ERR;
		if (hi == '\n')
			continue;
		if ((hi = hexdigcaps(hi)) == -1)
			return ENCODING_ERR;

		if ((lo = fgetc(fp)) == EOF)
			return ENCODING_ERR;
		if ((lo = hexdigcaps(lo)) == -1)
			return ENCODING_ERR;

		return (hi << 4) | lo;
	}
}

static int
encoding_getc_raw(FILE *fp, int high, int nul)
{
	int ch;

	if ((ch = fgetc(fp)) == EOF)
		return ENCODING_EOF;
	if (!high && (ch & 0x80))
		return ENCODING_ERR;
	if (!nul && ch == '\0')
		return ENCODING_ERR;
	return ch;
}

static int
hexdigcaps(int ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	return -1;
}
