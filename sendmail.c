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

#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "address.h"
#include "pathnames.h"
#include "sendmail.h"

static int cat(const char *);
static int setup_mail(const struct sendmail *, char *);

int
sendmail(int edit, struct sendmail *letter)
{
	char path[] = "/tmp/mail/send.XXXXXXXXXX";
	pid_t pid;
	int fd, rv, status;

	rv = -1;

	if (setup_mail(letter, path) == -1)
		return -1;

	switch (edit) {
		case EDIT_CAT:
			if (cat(path) == -1)
				goto fail;
			break;
		case EDIT_VI:
			switch (pid = fork()) {
			case -1:
				warn("fork");
				goto fail;
			case 0:
				execl(PATH_MAILZWRAPPER, "vi", path, NULL);
				err(1, "execl");
			default:
				if (waitpid(pid, &status, 0) == -1 || status != 0)
					goto fail;
			}
			break;
		case EDIT_MANUAL: {
			int c, ch, tm;

			/* accept "q\n" or "\n", nothing else. */
			if (fprintf(stderr, "message located at %s, press enter after editing"
					" or q to cancel\n", path) < 0)
				goto fail;
			if ((ch = fgetc(stdin)) == EOF)
				goto fail;
			tm = 0;
			while ((c = fgetc(stdin)) != EOF) {
				if (c == '\n')
					break;
				else
					tm = 1;
			}
			if (tm || ch != '\n') {
				if (unlink(path) == -1)
					return -1;
				if (tm || ch != 'q') {
					if (fprintf(stderr, "invalid response\n") < 0)
						return -1;
				}
				return 0;
			}
			break;
		}
		default:
			assert(0);
	}

	switch (pid = fork()) {
	case -1:
		warn("fork");
		goto fail;
	case 0:
		if ((fd = open(path, O_RDONLY)) == -1)
			err(1, "fopen");
		if (dup2(fd, STDIN_FILENO) == -1)
			err(1, "dup2");
		if (close(fd) == -1)
			err(1, "close");

		letter->to[letter->tl] = '\0';
		(void) execl(PATH_MAILZWRAPPER, "sendmail", letter->to, NULL);
		err(1, "execl");
	default:
		if (waitpid(pid, &status, 0) == -1 || status != 0)
			goto fail;
	}

	rv = 0;
	fail:
	if (unlink(path) == -1)
		rv = -1;
	return rv;
}

static int
cat(const char *path)
{
	FILE *fp;
	int c, rv;

	rv = -1;

	if ((fp = fopen(path, "a")) == NULL)
		return -1;
	while ((c = fgetc(stdin)) != EOF)
		if (fputc(c, fp) == EOF)
			goto fail;
	if (ferror(stdin))
		goto fail;

	rv = 0;
	fail:
	if (fclose(fp) == EOF)
		rv = -1;
	return rv;
}

static int
setup_mail(const struct sendmail *letter, char *path)
{
	int fd, rv;
	FILE *fp;
	time_t cur;
	struct tm *tm;
	char date[32];

	rv = -1;

	assert(letter->from.str != NULL);
	assert(letter->to != NULL);
	/* assert(letter->subject != NULL); */

	if ((cur = time(NULL)) == -1) {
		/* cant happen w/64 bit time... */
		warn("time");
		return -1;
	}

	if ((tm = localtime(&cur)) == NULL) {
		warnx("localtime");
		return -1;
	}

	if (strftime(date, 32, "%a, %d %b %Y %H:%M:%S %z", tm) == 0) {
		warnx("strftime");
		return -1;
	}

	if ((fd = mkstemp(path)) == -1) {
		warn("mkstemp");
		return -1;
	}
	if ((fp = fdopen(fd, "w")) == NULL) {
		warn("fdopen");
		(void) close(fd);
		(void) unlink(path);
		return -1;
	}

	if (fprintf(fp, "From: %s\n", letter->from.str) < 0) {
		warn("fprintf");
		goto fp;
	}
	if (fprintf(fp, "To: %.*s\n", letter->tl, letter->to) < 0) {
		warn("fprintf");
		goto fp;
	}
	if (letter->subject != NULL 
			&& fprintf(fp, "Subject: %s%s\n", letter->re ? "Re: " : "", letter->subject) < 0) {
		warn("fprintf");
		goto fp;
	}
	if (fprintf(fp, "Date: %s\n", date) < 0) {
		warn("fprintf");
		goto fp;
	}
	if (fputc('\n', fp) == EOF) {
		warn("fputc");
		goto fp;
	}

	if (letter->seed != NULL) {
		int c;

		while ((c = fgetc(letter->seed)) != EOF)
			if (fputc(c, fp) == EOF)
				goto fp;
		if (ferror(letter->seed))
			goto fp;
	}

	rv = 0;
	fp:
	if (fclose(fp) == EOF)
		rv = -1;
	if (rv == -1)
		(void) unlink(path);
	return rv;
}
