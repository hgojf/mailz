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

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charset.h"
#include "string-util.h"
#include "macro.h"

static int utf8_getchar(mbstate_t *, struct encoding *, FILE *, 
	char [static 4]);

int
charset_getchar(FILE *fp, struct charset *charset, char buf[static 4])
{
	int c;

	switch (charset->type) {
	case CHARSET_ASCII:
	case CHARSET_OTHER:
		if ((c = encoding_getbyte(fp, &charset->encoding)) == ENCODING_EOF)
			return 0;

		if (!isascii(c)) {
			if (charset->type == CHARSET_OTHER)
				goto replace;
			else
				return -1;
		}

		if (!isprint(c) && !isspace(c))
			goto replace;

		buf[0] = c;
		return 1;
	case CHARSET_UTF8:
		return utf8_getchar(&charset->val.utf8, &charset->encoding, 
			fp, buf);
	}

	/* invalid charset type */
	abort();

	replace:
	/* utf8 replacement character */
	memcpy(buf, "\xEF\xBF\xBD", 3);
	return 3;
}

int
charset_set(struct charset *charset, const char *type, size_t tl)
{
	struct {
		const char *ident;
		enum charset_type type;
	} charsets[] = {
		{ "utf-8", CHARSET_UTF8 },
		{ "us-ascii", CHARSET_ASCII },
	};

	for (size_t i = 0; i < nitems(charsets); i++) {
		if (bounded_strcasecmp(charsets[i].ident, type, tl) != 0)
			continue;

		charset_set_type(charset, charsets[i].type);
		return 0;
	}

	return -1;
}

void
charset_set_type(struct charset *charset, enum charset_type type)
{
	memset(&charset->val, 0, sizeof(charset->val));
	charset->type = type;
}

static int
utf8_getchar(mbstate_t *mbs, struct encoding *encoding, FILE *fp, char buf[static 4])
{
	int c;

	for (int i = 0; i < 4; i++) {
		if ((c = encoding_getbyte(fp, encoding)) == ENCODING_ERR)
			return -1;
		if (c == ENCODING_EOF) {
			if (i == 0)
				return 0;
			return -1;
		}

		buf[i] = c;
		switch (mbrtowc(NULL, &buf[i], 1, mbs)) {
		case (size_t)-1:
		case (size_t)-3:
			return -1;
		case 0:
			goto replace;
		case (size_t)-2:
			break;
		default:
			if (i == 0 && !isprint(c) && !isspace(c))
				goto replace;
			return i + 1;
		}
	}

	return -1;

	replace:
	/* utf8 replacement character */
	memcpy(buf, "\xEF\xBF\xBD", 3);
	return 3;
}
