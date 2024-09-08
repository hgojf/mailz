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

#ifndef MAILZ_ENCODING_H
#define MAILZ_ENCODING_H
struct encoding {
	enum encoding_type {
		ENCODING_7BIT,
		ENCODING_8BIT,
		ENCODING_BASE64,
		ENCODING_BINARY,
		ENCODING_QUOTED_PRINTABLE,
	} type;

	union {
		struct b64_decode {
			char buf[2]; /* extra chars */
			int i; /* what idx of buf has a char, or end if none */
			int end;
		} base64;
	} val;
};

/* out of range for a signed char */
#define ENCODING_EOF -129
#define ENCODING_ERR -130

int encoding_getbyte(FILE *, struct encoding *);
int encoding_set(struct encoding *, const char *);
void encoding_set_type(struct encoding *, enum encoding_type);
#endif /* MAILZ_ENCODING_H */
