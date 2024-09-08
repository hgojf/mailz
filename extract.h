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

#ifndef MAILZ_EXTRACT_H
#define MAILZ_EXTRACT_H
#include <sys/queue.h>

#include <imsg.h>
#include <stdio.h>
#include <unistd.h>

#include "maildir-extract.h"

struct extract {
	struct imsgbuf msgbuf;
	FILE *e;
	int fd;
	pid_t pid;
};

struct extracted_header {
	char *key;
	enum extract_header_type type;
	union extract_header_val {
		/* EXTRACT_DATE */
		time_t date;
		/* EXTRACT_FROM */
		struct {
			char *addr;
			char *name;
		} from;
		/* EXTRACT_MESSAGE_ID, EXTRACT_STRING */
		char *string;
	} val;
};

void extract_header_free(enum extract_header_type, 
	union extract_header_val *);

int maildir_extract(struct extract *, struct extracted_header *, size_t);
int maildir_extract_close(struct extract *);

int maildir_extract_next(struct extract *, int, 
	struct extracted_header *, size_t);

int maildir_extract_quick(int, struct extracted_header *, size_t);
#endif /* MAILZ_EXTRACT_H */
