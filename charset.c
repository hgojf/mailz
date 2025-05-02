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

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <uchar.h>
#include <wchar.h>

#include "charset.h"
#include "encoding.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

static int charset_raw(struct encoding *, FILE *, int, char [static 4]);
static int charset_iso_8859_1(struct encoding *, FILE *, char [static 4]);
static int charset_utf8(struct encoding *, FILE *, char [static 4]);

#define CTR_ASCII 0x1

static const struct {
	const char *ident;
	enum charset_type type;
} charsets[] = {
	{ "iso-8859-1", CHARSET_ISO_8859_1 },
	{ "us-ascii",	CHARSET_ASCII },
	{ "utf-8",	CHARSET_UTF8 },
};

int
charset_from_name(struct charset *c, const char *name)
{
	size_t i;

	for (i = 0; i < nitems(charsets); i++) {
		if (!strcasecmp(name, charsets[i].ident)) {
			charset_from_type(c, charsets[i].type);
			return 0;
		}
	}

	return -1;
}

void
charset_from_type(struct charset *c, enum charset_type type)
{
	c->type = type;
}

int
charset_getc(struct charset *c, struct encoding *encoding, FILE *fp,
	     char buf[static 4])
{
	switch (c->type) {
	case CHARSET_ASCII:
		return charset_raw(encoding, fp, CTR_ASCII, buf);
	case CHARSET_ISO_8859_1:
		return charset_iso_8859_1(encoding, fp, buf);
	case CHARSET_OTHER:
		return charset_raw(encoding, fp, 0, buf);
	case CHARSET_UTF8:
		return charset_utf8(encoding, fp, buf);
	default:
		return -1;
	}
}

static int
charset_iso_8859_1(struct encoding *encoding, FILE *fp,
		   char buf[static 4])
{
	mbstate_t mbs;
	size_t n;
	char32_t uc;
	int ch;

	memset(&mbs, 0, sizeof(mbs));
	if ((ch = encoding_getc(encoding, fp)) == ENCODING_ERR)
		return -1;
	if (ch == ENCODING_EOF)
		return 0;
	uc = ch;

	if (MB_CUR_MAX > 4)
		return -1;
	if ((n = c32rtomb(buf, uc, &mbs)) == (size_t)-1)
		return -1;

	return n;
}

static int
charset_raw(struct encoding *e, FILE *fp, int flags,
	    char buf [static 4])
{
	int ch;

	if ((ch = encoding_getc(e, fp)) == ENCODING_ERR)
		return -1;
	if (ch == ENCODING_EOF)
		return 0;

	if (ch > 127) {
		if (flags & CTR_ASCII)
			return -1;
		/* UTF-8 replacement character */
		memcpy(buf, "\xEF\xBF\xBD", 3);
		return 3;
	}

	buf[0] = ch;
	return 1;
}

static int
charset_utf8(struct encoding *e, FILE *fp, char buf[static 4])
{
	mbstate_t mbs;
	int i;

	memset(&mbs, 0, sizeof(mbs));
	for (i = 0; i < 4; i++) {
		int ch;
		char cc;

		if ((ch = encoding_getc(e, fp)) == ENCODING_ERR)
			return -1;
		if (ch == ENCODING_EOF) {
			if (i == 0)
				return 0;
			return -1;
		}

		cc = ch;

		switch (mbrtowc(NULL, &cc, 1, &mbs)) {
		case -1:
		case -3:
			return -1;
		case -2:
			buf[i] = ch;
			continue;
		default:
			buf[i] = ch;
			return i + 1;
		}
	}

	return -1;
}
