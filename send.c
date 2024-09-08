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

#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "conf.h"
#include "date.h"
#include "extract.h"
#include "letter.h"
#include "pathnames.h"
#include "read-letter.h"
#include "send.h"

#define MAIL_LINE_MAX 996 /* not including the CRLF */
#define PROMPT_ERR 0
#define PROMPT_OK 1
#define PROMPT_QUIT 2

static int print_folding(FILE *, const char *, int *);
static int print_quotedate(FILE *, const char *, time_t);
static int print_references(FILE *, const char *, const char *, 
	const char *);
static int prompt(const char *);
static int sendmail(int);

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

/*
 * XXX: the line folding performed in these functions is too involved,
 * and would be helped by a higher level interface.
 */

static int
header_print_addr(FILE *fp, const char *key, const char *addr, 
	const char *name)
{
	int n, m;

	n = fprintf(fp, "%s: ", key);
	if (n < 0 || n >= MAIL_LINE_MAX)
		return -1;

	if (name != NULL)
		m = fprintf(fp, "%s <%s>", name, addr);
	else
		m = fprintf(fp, "%s", addr);

	if (m < 0 || n + m > MAIL_LINE_MAX)
		return -1;

	if (fputc('\n', fp) == EOF)
		return -1;

	return 0;
}

static int
header_print_date(FILE *fp, const char *key, time_t date)
{
	char dbuf[EMAIL_DATE_LEN];
	struct tm *tm;
	int n;

	if ((tm = localtime(&date)) == NULL)
		return -1;
	if (date_format(tm, tm->tm_gmtoff, dbuf) == -1)
		return -1;

	n = fprintf(fp, "%s: %s", key, dbuf);
	if (n < 0 || n >= MAIL_LINE_MAX)
		return -1;

	if (fputc('\n', fp) == EOF)
		return -1;

	return 0;
}

static int
header_print_normal(FILE *fp, const char *key, const char *val)
{
	int n;

	n = fprintf(fp, "%s: ", key);
	if (n < 0 || n >= MAIL_LINE_MAX)
		return -1;

	if (print_folding(fp, val, &n) == -1)
		return -1;

	if (fputc('\n', fp) == EOF)
		return -1;

	return 0;
}

static int
print_folding(FILE *fp, const char *s, int *off)
{
	for (; *s != '\0'; s++, (*off)++) {
		if (*off == MAIL_LINE_MAX) {
			if (fputs("\n ", fp) == EOF)
				return -1;
			*off = 0;
		}

		if (fputc(*s, fp) == EOF)
			return -1;
	}

	return 0;
}

static int
print_quotedate(FILE *fp, const char *who, time_t time)
{
	char db[EMAIL_DATE_LEN];
	struct tm *tm;
	int n;

	if ((tm = localtime(&time)) == NULL)
		return -1;

	if (date_format(tm, tm->tm_gmtoff, db) == -1)
		return -1;

	n = fprintf(fp, "On ");
	if (n < 0)
		return -1;

	if (print_folding(fp, db, &n) == -1)
		return -1;
	if (print_folding(fp, ", ", &n) == -1)
		return -1;
	if (print_folding(fp, who, &n) == -1)
		return -1;
	if (print_folding(fp, " wrote:\n", &n) == -1)
		return -1;

	return 0;
}

static int
print_references(FILE *fp, const char *in_reply_to, const char *message_id, 
	const char *references)
{
	int n;

	n = fprintf(fp, "References: ");
	if (n < 0)
		return -1;

	if (references == NULL)
		references = in_reply_to;

	if (references != NULL) {
		if (print_folding(fp, references, &n) == -1)
			return -1;
		if (message_id != NULL) {
			if (print_folding(fp, " ", &n) == -1)
				return -1;
		}
	}

	if (message_id != NULL)
		if (print_folding(fp, message_id, &n) == -1)
			return -1;

	if (fputc('\n', fp) == EOF)
		return -1;

	return 0;
}

static int
prompt(const char *path)
{
	int c;

	printf("letter located at %s\n"
			"Press enter after editing or q to cancel: ", path);
	if ((c = fgetc(stdin)) == EOF)
		return PROMPT_ERR;

	if (c != '\n') {
		int c1, nc;

		/* make sure we pick up the whole line */
		for (nc = 0; (c1 = fgetc(stdin)) != EOF && c1 != '\n'; nc++)
			;

		if (nc != 0)
			return PROMPT_ERR;

		if (c == 'q')
			return PROMPT_QUIT;

		return PROMPT_ERR;
	}

	return PROMPT_OK;
}

/* this takes ownership of fd */
static int
quote_body(FILE *fp, int fd)
{
	struct ignore ignore;
	struct reorder reorder;
	struct read_letter rl;

	memset(&ignore, 0, sizeof(ignore));
	memset(&reorder, 0, sizeof(reorder));

	ignore.type = IGNORE_ALL;

	if (maildir_read_letter(&rl, fd, 0, 0, &ignore, &reorder) == -1)
		return -1;

	if (fputs("> ", fp) == EOF)
		return -1;
	for (int lastnl = 2;;) {
		char buf[4];
		int n;

		if ((n = maildir_read_letter_getc(&rl, buf)) == -1)
			goto rl;
		if (n == 0)
			break;

		if ((n == 1 && buf[0] == '\n') || lastnl + n > MAIL_LINE_MAX) {
			if (fputs("\n> ", fp) == EOF)
				goto rl;
			lastnl = 2;
		}

		if (n != 1 || buf[0] != '\n') {
			if (fwrite(buf, n, 1, fp) != 1)
				goto rl;
			lastnl += n;
		}
	}

	if (maildir_read_letter_close(&rl) == -1)
		return -1;
	return 0;

	rl:
	maildir_read_letter_close(&rl);
	return -1;
}

int
reply(int cur, struct address *addr, struct letter *letter)
{
	struct extracted_header headers[4];
	char path[] = PATH_TMPDIR "/reply.XXXXXX";
	FILE *fp;
	int fd, fd1;

	if (addr->addr == NULL)
		return -1;

	if ((fd = mkostemp(path, O_CLOEXEC)) == -1)
		return -1;
	if ((fp = fdopen(fd, "w+")) == NULL) {
		close(fd);
		unlink(path);
		return -1;
	}

	if (header_print_date(fp, "Date", time(NULL)) == -1)
		goto fp;
	if (header_print_addr(fp, "From", addr->addr, addr->name) == -1)
		goto fp;

	if (letter->subject != NULL) {
		if (!strncmp(letter->subject, "Re: ", 4)) {
			if (header_print_normal(fp, "Subject", letter->subject) == -1)
				goto fp;
		}
		else {
			int n;

			n = fprintf(fp, "Subject: Re: ");
			if (n < 0)
				goto fp;
			if (print_folding(fp, letter->subject, &n) == -1)
				goto fp;
			if (fputc('\n', fp) == EOF)
				goto fp;
		}
	}

	headers[0].key = "In-Reply-To";
	headers[0].type = EXTRACT_STRING;

	headers[1].key = "Message-ID";
	headers[1].type = EXTRACT_STRING;

	headers[2].key = "References";
	headers[2].type = EXTRACT_STRING;

	headers[3].key = "Reply-To";
	headers[3].type = EXTRACT_FROM;

	if ((fd1 = openat(cur, letter->path, O_RDONLY | O_CLOEXEC)) == -1)
		goto fp;
	if (maildir_extract_quick(fd1, headers, nitems(headers)) == -1)
		goto fp;

	if (headers[3].val.from.addr != NULL) {
		if (header_print_addr(fp, "To", headers[3].val.from.addr,
			headers[3].val.from.name) == -1)
				goto headers;
	}
	else
		if (header_print_addr(fp, "To", letter->from.addr, 
			letter->from.name) == -1)
				goto headers;

	if (headers[1].val.string != NULL)
		if (header_print_normal(fp, "In-Reply-To", 
			headers[1].val.string) == -1)
				goto headers;

	if (print_references(fp, headers[0].val.string, headers[1].val.string, 
		headers[2].val.string) == -1)
			goto headers;

	if (fputc('\n', fp) == EOF)
		goto headers;

	if (letter->from.name != NULL) {
		if (print_quotedate(fp, letter->from.name, letter->date) == -1)
			goto headers;
	}
	else
		if (print_quotedate(fp, letter->from.addr, letter->date) == -1)
			goto headers;

	/* XXX: TOCTOU */
	if ((fd1 = openat(cur, letter->path, O_RDONLY | O_CLOEXEC)) == -1)
		goto headers;
	if (quote_body(fp, fd1) == -1)
		goto headers;

	if (fflush(fp) == EOF)
		goto headers;

	switch (prompt(path)) {
	case PROMPT_ERR:
		goto headers;
	case PROMPT_OK:
		break;
	case PROMPT_QUIT:
		goto good;
	}

	if (fseek(fp, 0, SEEK_SET) == -1)
		goto headers;
	if (sendmail(fd) == -1)
		goto headers;

	good:
	for (size_t i = 0; i < nitems(headers); i++)
		extract_header_free(headers[i].type, &headers[i].val);

	unlink(path);
	fclose(fp);
	return 0;

	headers:
	for (size_t i = 0; i < nitems(headers); i++)
		extract_header_free(headers[i].type, &headers[i].val);
	fp:
	unlink(path);
	fclose(fp);
	return -1;
}

int
send(const char *subject, int argc, char **argv, FILE *fp, FILE *in,
	struct address *addr)
{
	int fd;

	if (addr->addr == NULL)
		return -1;

	if ((fd = fileno(fp)) == -1)
		return -1;

	if (header_print_date(fp, "Date", time(NULL)) == -1)
		return -1;
	if (header_print_addr(fp, "From", addr->addr, addr->name) == -1)
		return -1;
	if (header_print_normal(fp, "Subject", subject) == -1)
		return -1;

	if (argc != 0) {
		int n;

		n = fprintf(fp, "To: ");

		if (n < 0)
			return -1;

		for (int i = 0; i < argc; i++) {
			size_t ml;
			int m;

			if (i == argc - 1) /* last man */
				ml = strlen(argv[i]);
			else
				ml = strlen(argv[i]) + strlen(", ");

			/*
			 * This tries to prevent folding in the middle of an email
			 * address. This shouldnt really be possible since
			 * email addresses are at max 320 and the longest line
			 * is 996+CRLF. If we want to fold at 72 character 
			 * boundaries some thought will have to be put in for
			 * where it is ok to put folding in an address.
			 */

			if (ml > MAIL_LINE_MAX)
				return -1;

			if ((size_t)n + ml > MAIL_LINE_MAX) {
				if (fputs("\n ", fp) == EOF)
					return -1;
				n = 1;
			}

			if (i == argc - 1)
				m = fprintf(fp, "%s", argv[i]);
			else
				m = fprintf(fp, "%s, ", argv[i]);

			if (m < 0)
				return -1;
			n += m;
		}

		if (fputc('\n', fp) == EOF)
			return -1;
	}

	if (fputc('\n', fp) == EOF)
		return -1;

	for (int lastnl = 0, c = fgetc(in); c != EOF; c = fgetc(in)) {
		if (lastnl == MAIL_LINE_MAX) {
			if (fputc('\n', fp) == EOF)
				return -1;
			lastnl = 0;
		}

		if (fputc(c, fp) == EOF)
			return -1;

		if (c == '\n')
			lastnl = 0;
		else
			lastnl++;
	}
	if (ferror(in) == EOF)
		return -1;

	if (fseek(fp, 0, SEEK_SET) == EOF)
		return -1;
	if (fflush(fp) == EOF)
		return -1;

	if (sendmail(fd) == -1)
		return -1;

	return 0;
}

static int
sendmail(int fd)
{
	pid_t pid;
	int status;

	switch (pid = fork()) {
	case -1:
		return -1;
	case 0:
		if (dup2(fd, STDIN_FILENO) == -1)
			err(1, "dup2");
		execl(PATH_SENDMAIL, "sendmail", "-t", NULL);
		err(1, "%s", PATH_SENDMAIL);
	default:
		break;
	}

	if (waitpid(pid, &status, 0) == -1 || WEXITSTATUS(status) != 0)
		return -1;

	return 0;
}
