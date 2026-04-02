/*
 * Copyright (c) 2026 Henry Ford <fordhenry2299@gmail.com>

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

#ifndef COMMAND_H
#define COMMAND_H

enum {
	COMMAND_OK,
	COMMAND_EMPTY,
	COMMAND_EOF,
	COMMAND_INVALID,
	COMMAND_LONG,
	COMMAND_THREAD_EOF,
};

struct command_letter {
	size_t num;
	int thread;
};

struct command_lexer {
	FILE *fp;
	int eol;
};

void command_init(struct command_lexer *, FILE *);
int command_letter(struct command_lexer *, struct command_letter *);
int command_name(struct command_lexer *, char *, size_t);

#endif /* COMMAND_H */
