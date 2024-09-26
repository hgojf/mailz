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

#ifndef MAILZ_CONF_H
#define MAILZ_CONF_H
struct address {
	char *addr;
	char *name;
};

struct ignore {
	size_t argc;
	char **argv;
	enum {
		IGNORE_IGNORE,
		IGNORE_RETAIN,
		IGNORE_ALL,
	} type;
};

struct reorder {
	size_t argc;
	char **argv;
};

struct mailz_conf {
	struct address address;
	struct ignore ignore;
	struct reorder reorder;
	int cache;
	int linewrap;
};

void config_default(struct mailz_conf *);
FILE *config_file(void);
void config_free(struct mailz_conf *);
int config_init(struct mailz_conf *, FILE *, const char *);
#endif /* MAILZ_CONF_H */
