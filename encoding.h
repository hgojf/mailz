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

#ifndef ENCODING_H
#define ENCODING_H
enum encoding_type {
	#define ENCODING_UNKNOWN -1
	ENCODING_7BIT,
	ENCODING_8BIT,
	ENCODING_BASE64,
	ENCODING_BINARY,
	ENCODING_QP,
};

struct encoding {
	union {
		struct encoding_b64 {
			char buf[2];
			int start;
			int end;
		} b64;
	} v;

	enum encoding_type type;
};

#define ENCODING_EOF -129
#define ENCODING_ERR -130

int encoding_from_name(const char *);
void encoding_from_type(struct encoding *, enum encoding_type);
int encoding_getc(struct encoding *, FILE *);
#endif /* ! ENCODING_H */
