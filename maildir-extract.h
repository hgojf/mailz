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

#ifndef MAILZ_MAILDIR_EXTRACT_H
#define MAILZ_MAILDIR_EXTRACT_H
/* parent -> maildir-extract */
enum {
	IMSG_MDE_HEADERDEF,
	IMSG_MDE_LETTER,
};

/* maildir-extract -> parent */
enum {
	IMSG_MDE_HEADER,
	IMSG_MDE_HEADERDONE,
};

enum extract_header_type {
	EXTRACT_DATE,
	EXTRACT_FROM,
	EXTRACT_STRING,
};
#endif /* MAILZ_MAILDIR_EXTRACT_H */
