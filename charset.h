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

#ifndef CHARSET_H
#define CHARSET_H

#include "encoding.h"

enum charset_type {
	CHARSET_ASCII,
	CHARSET_ISO_8859_1,
	CHARSET_OTHER,
	CHARSET_UTF8,
};

struct charset {
	enum charset_type type;
	int error;
};

int charset_from_name(struct charset *, const char *);
void charset_from_type(struct charset *, enum charset_type);
int charset_getc(struct charset *, struct encoding *, FILE *, char [static 4]);

#endif /* ! CHARSET_H */
