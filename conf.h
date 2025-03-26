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

struct mailz_conf_mailbox {
	char *ident;
	char *maildir;
	char address[255];
	RB_ENTRY(mailz_conf_mailbox) entries;
};

struct mailz_conf {
	char address[255];
	struct mailz_ignore {
		char **headers;
		size_t nheader;
		#define MAILZ_IGNORE_IGNORE 0
		#define MAILZ_IGNORE_RETAIN 1
		int type;
	} ignore;
	RB_HEAD(mailz_conf_mailboxes, mailz_conf_mailbox) mailboxes;
};

struct mailz_conf_mailbox *mailz_conf_mailbox(struct mailz_conf *, char *);
void mailz_conf_free(struct mailz_conf *);
int mailz_conf_init(struct mailz_conf *);

#endif /* ! CONF_H */
