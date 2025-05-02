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

#ifndef CONTENT_PROC_H
#define CONTENT_PROC_H

#include <sys/queue.h>

#include <imsg.h>

#include "content.h"

struct content_proc {
	struct imsgbuf msgbuf;
	pid_t pid;
};

#define CNT_IGNORE_IGNORE 0
#define CNT_IGNORE_RETAIN 1

int content_proc_ignore(struct content_proc *, const char *, int);
int content_proc_init(struct content_proc *, const char *);
int content_proc_kill(struct content_proc *);
int content_proc_reply(struct content_proc *, FILE *, const char *, int, int);
int content_proc_summary(struct content_proc *, struct content_summary *, int);

struct content_letter {
	struct content_proc *pr;
	FILE *fp;
};

void content_letter_close(struct content_letter *);
int content_letter_finish(struct content_letter *);
int content_letter_getc(struct content_letter *, char [static 4]);
int content_letter_init(struct content_proc *, struct content_letter *, int);

#endif /* ! CONTENT_PROC_H */
