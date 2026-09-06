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

#ifndef CONF_H
#define CONF_H

#include <sys/tree.h>

struct config_mailbox {
	char *ident;
	char *maildir;
	char address[255];
	RB_ENTRY(config_mailbox) entries;
};

struct config {
	char address[255];
	struct config_ignore {
		char **headers;
		size_t nheader;
		int retain;
	} ignore;
	RB_HEAD(config_mailboxes, config_mailbox) mailboxes;
};

struct config_mailbox *config_mailbox(struct config *, char *);
void config_free(struct config *);
void parse_config(struct config *, const char *);

#endif /* ! CONF_H */
