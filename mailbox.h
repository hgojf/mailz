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
 
#ifndef MAILZ_MAILBOX_H
#define MAILZ_MAILBOX_H
struct letter {
	char *subject;
	struct from_safe from;
	time_t date;

	char *path;
};

struct mailbox {
	long long nletters;
	struct letter *letters;

	DIR *cur;

	/* cache stuff */
	#define MAILBOX_CACHE_READ 1 /* read the cache */
	#define MAILBOX_CACHE_UPDATE 2 /* read and update the cache */
	int do_cache;
	int view_seen;
	const char *root;
};

void mailbox_free(struct mailbox *);
int mailbox_setup(const char *, struct mailbox *);
int mailbox_read(struct mailbox *, int, int);
int mailbox_print(struct mailbox *, size_t, size_t);
int mailbox_letter_print(size_t, struct letter *);
int mailbox_letter_print_content(struct mailbox *,
	struct letter *, FILE *);
int mailbox_letter_print_read(struct mailbox *, struct letter *,
	const struct options *, FILE *);
int mailbox_letter_mark_read(struct mailbox *, struct letter *);
int mailbox_letter_mark_unread(struct mailbox *, struct letter *);

#ifdef REGRESS
int letter_test(void);
#endif /* REGRESS */
#endif /* MAILZ_MAILBOX_H */
