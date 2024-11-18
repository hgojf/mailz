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

#include <sys/queue.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <imsg.h>
#include <limits.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "charset.h"
#include "content.h"
#include "encoding.h"
#include "imsg-blocking.h"

#define HEADER_NAME_LEN 996

#define HL_EOF -129
#define HL_ERR -130
#define HL_PIPE -131

struct header_lex {
	int cstate;
	int qstate;
	int skipws;
	FILE *echo;
};

struct ignore {
	char **headers;
	size_t nheader;
	#define IGNORE_IGNORE 0
	#define IGNORE_RETAIN 1
	int type;
};

struct from {
	char *addr;
	char *name;
	size_t addrsz;
	size_t namesz;
};

static int handle_ignore(struct imsg *, struct ignore *, int);
static int handle_letter(struct imsgbuf *, struct imsg *, struct ignore *);
static int handle_reply(struct imsgbuf *, struct imsg *);
static int handle_summary(struct imsgbuf *, struct imsg *);
static int header_content_type(FILE *, FILE *, int, struct charset *,
			       struct encoding *);
static time_t header_date(FILE *);
static int header_encoding(FILE *, FILE *, int, struct encoding *);
static int header_from(FILE *, struct from *);
static int header_lex(FILE *, struct header_lex *);
static int header_message_id(FILE *, char *, size_t);
static int header_name(FILE *, char *, size_t);
static int header_references(FILE *, struct imsgbuf *);
static int header_skip(FILE *, FILE *, int);
static int header_subject(FILE *, char *, size_t);
static int ignore_header(const char *, struct ignore *);
static FILE *imsg_get_fp(struct imsg *, const char *);
static void strip_trailing(char *);
static void usage(void);

static int
handle_ignore(struct imsg *msg, struct ignore *ignore, int type)
{
	struct content_header header;
	char *s, **t;

	if (ignore->nheader == SIZE_MAX)
		return -1;

	if (imsg_get_data(msg, &header, sizeof(header)) == -1)
		return -1;

	if (strnlen(header.name, sizeof(header.name))
			== sizeof(header.name))
		return -1;

	if ((s = strdup(header.name)) == NULL)
		return -1;

	t = reallocarray(ignore->headers, ignore->nheader + 1,
			 sizeof(*ignore->headers));
	if (t == NULL) {
		free(s);
		return -1;
	}

	ignore->headers = t;
	ignore->headers[ignore->nheader++] = s;
	ignore->type = type;
	return 0;
}

static int
handle_letter(struct imsgbuf *msgbuf, struct imsg *msg,
	      struct ignore *ignore)
{
	struct imsg msg2;
	struct charset charset;
	struct encoding encoding;
	FILE *in, *out;
	ssize_t n;
	int flags, got_content_type, got_encoding, rv;

	rv = -1;

	if (imsg_get_data(msg, &flags, sizeof(flags)) == -1)
		return -1;

	if ((n = imsg_get_blocking(msgbuf, &msg2)) == -1)
		return -1;
	if (n == 0)
		return -1;

	if (imsg_get_type(&msg2) != IMSG_CNT_LETTERPIPE)
		goto msg2;
	if ((out = imsg_get_fp(&msg2, "w")) == NULL)
		goto msg2;

	if ((in = imsg_get_fp(msg, "r")) == NULL)
		goto out;

	charset_from_type(&charset, CHARSET_ASCII);
	encoding_from_type(&encoding, ENCODING_7BIT);
	got_content_type = 0;
	got_encoding = 0;
	for (;;) {
		char buf[HEADER_NAME_LEN];
		int echo, hv;

		if ((hv = header_name(in, buf, sizeof(buf))) == -1)
			goto in;
		if (hv == 0)
			break;

		if (flags & CNT_LR_NOHDR || ignore_header(buf, ignore))
			echo = 0;
		else
			echo = 1;

		if (echo) {
			if (fprintf(out, "%s:", buf) < 0) {
				if (ferror(out) && errno == EPIPE)
					goto done;
				goto in;
			}
		}

		if (!strcasecmp(buf, "content-transfer-encoding")) {
			if (got_encoding)
				goto in;
			if ((hv = header_encoding(in, out, echo, &encoding)) == -1)
				goto in;
			if (hv == 0)
				goto done;
			got_encoding = 1;
		}
		else if (!strcasecmp(buf, "content-type")) {
			if (got_content_type)
				goto in;
			if ((hv = header_content_type(in, out, echo,
						      &charset, &encoding)) == -1)
				goto in;
			if (hv == 0)
				goto done;

			got_content_type = 1;
		}
		else {
			if ((hv = header_skip(in, out, echo)) == -1)
				goto in;
			if (hv == 0)
				goto done;
		}
	}

	if (fputc('\n', out) == EOF) {
		if (ferror(out) && errno == EPIPE)
			goto done;
	}

	for (;;) {
		char buf[4];
		int n;

		if ((n = charset_getc(&charset, &encoding, in,
				      buf)) == -1)
			goto in;
		if (n == 0)
			break;

		if (n == 1) {
			int ch;

			ch = (unsigned char)buf[0];
			if (!isprint(ch) && !isspace(ch)) {
				/* UTF-8 replacement character */
				memcpy(buf, "\xEF\xBF\xBD", 3);
				n = 3;
			}
		}

		if (fwrite(buf, n, 1, out) != 1) {
			if (ferror(out) && errno == EPIPE)
				goto done;
			goto in;
		}
	}

	done:
	rv = 0;
	in:
	fclose(in);
	out:
	fclose(out);
	msg2:
	imsg_free(&msg2);
	return rv;
}

static int
handle_reply(struct imsgbuf *msgbuf, struct imsg *msg)
{
	struct content_reply_summary sm;
	FILE *fp;
	off_t ref;
	int rv;

	rv = -1;

	if ((fp = imsg_get_fp(msg, "r")) == NULL)
		return -1;

	memset(&sm, 0, sizeof(sm));
	ref = -1;
	for (;;) {
		char buf[HEADER_NAME_LEN];
		int hv;

		if ((hv = header_name(fp, buf, sizeof(buf))) == -1)
			goto fp;
		if (hv == 0)
			break;

		if (!strcasecmp(buf, "from")) {
			struct from from;

			if (strlen(sm.name) != 0)
				goto fp;

			from.addr = NULL;
			from.addrsz = 0;

			from.name = sm.name;
			from.namesz = sizeof(sm.name);

			if (header_from(fp, &from) == -1)
				goto fp;
		}
		if (!strcasecmp(buf, "in-reply-to")) {
			if (strlen(sm.in_reply_to) != 0)
				goto fp;
			if (header_message_id(fp, sm.in_reply_to,
					      sizeof(sm.in_reply_to)) == -1)
				goto fp;
		}
		else if (!strcasecmp(buf, "message-id")) {
			if (strlen(sm.message_id) != 0)
				goto fp;
			if (header_message_id(fp, sm.message_id,
					      sizeof(sm.message_id)) == -1)
				goto fp;
		}
		else if (!strcasecmp(buf, "references")) {
			if (ref != -1)
				goto fp;
			if ((ref = ftello(fp)) == -1)
				goto fp;
			if (header_skip(fp, NULL, 0) == -1)
				goto fp;
		}
		else if (!strcasecmp(buf, "reply-to")) {
			struct from from;

			if (strlen(sm.reply_to.addr) != 0)
				goto fp;

			from.addr = sm.reply_to.addr;
			from.addrsz = sizeof(sm.reply_to.addr);

			from.name = sm.reply_to.name;
			from.namesz = sizeof(sm.reply_to.name);

			if (header_from(fp, &from) == -1)
				goto fp;
			if (strlen(sm.reply_to.addr) == 0)
				goto fp;
		}
		else {
			if (header_skip(fp, NULL, 0) == -1)
				goto fp;
		}
	}

	if (imsg_compose(msgbuf, IMSG_CNT_REPLY, 0, -1, -1,
			 &sm, sizeof(sm)) == -1)
		goto fp;

	if (ref != -1) {
		if (fseeko(fp, ref, SEEK_SET) == EOF)
			goto fp;
		if (header_references(fp, msgbuf) == -1)
			goto fp;
	}

	if (imsg_compose(msgbuf, IMSG_CNT_REFERENCEOVER, 0, -1, -1,
			 NULL, 0) == -1)
		goto fp;

	if (imsg_flush_blocking(msgbuf) == -1)
		goto fp;

	rv = 0;
	fp:
	fclose(fp);
	return rv;
}

static int
handle_summary(struct imsgbuf *msgbuf, struct imsg *msg)
{
	struct content_summary sm;
	FILE *fp;
	int rv;

	rv = -1;

	if ((fp = imsg_get_fp(msg, "r")) == NULL)
		return -1;

	memset(&sm, 0, sizeof(sm));
	sm.date = -1;
	for (;;) {
		char buf[HEADER_NAME_LEN];
		int n;

		if ((n = header_name(fp, buf, sizeof(buf))) == -1)
			goto fp;
		if (n == 0)
			break;

		if (!strcasecmp(buf, "date")) {
			if (sm.date != -1)
				goto fp;
			if ((sm.date = header_date(fp)) == -1)
				goto fp;
		}
		else if (!strcasecmp(buf, "from")) {
			struct from from;

			if (strlen(sm.from) != 0)
				goto fp;

			from.addr = sm.from;
			from.addrsz = sizeof(sm.from);

			from.name = NULL;
			from.namesz = 0;

			if (header_from(fp, &from) == -1)
				goto fp;
			if (strlen(sm.from) == 0)
				goto fp;
		}
		else if (!strcasecmp(buf, "subject")) {
			if (sm.have_subject)
				goto fp;
			if (header_subject(fp, sm.subject,
					   sizeof(sm.subject)) == -1)
				goto fp;
			sm.have_subject = 1;
		}
		else {
			if (header_skip(fp, NULL, 0) == -1)
				goto fp;
			continue;
		}

		if (sm.date != -1 && strlen(sm.from) != 0
				  && sm.have_subject)
			break;
	}

	if (sm.date == -1)
		goto fp;
	if (strlen(sm.from) == 0)
		goto fp;

	if (imsg_compose(msgbuf, IMSG_CNT_SUMMARY, 0, -1, -1,
			 &sm, sizeof(sm)) == -1)
		goto fp;
	if (imsg_flush_blocking(msgbuf) == -1)
		goto fp;

	rv = 0;
	fp:
	fclose(fp);
	return rv;
}

static time_t
header_date(FILE *fp)
{
	struct header_lex lex;
	struct tm tm;
	char buf[100];
	const char *end, *fmt;
	size_t n;
	time_t date;
	long off;

	lex.cstate = 0;
	lex.echo = NULL;
	lex.qstate = 0;
	lex.skipws = 1;
	fmt = "%d %b %Y %H:%M:%S %z";
	n = 0;
	for (;;) {
		int ch;

		if ((ch = header_lex(fp, &lex)) == HL_ERR)
			return -1;
		if (ch == HL_EOF)
			break;

		if (ch == ',')
			fmt = "%a, %d %b %Y %H:%M:%S %z";

		if (n == sizeof(buf) - 1)
			return -1;
		buf[n++] = ch;
	}

	buf[n] = '\0';

	memset(&tm, 0, sizeof(tm));
	if ((end = strptime(buf, fmt, &tm)) == NULL)
		return -1;

	if (end[strspn(end, " \t")] != '\0')
		return -1;

	off = tm.tm_gmtoff;
	if ((date = timegm(&tm)) == -1)
		return -1;

	return date - off;
}

static int
header_content_type(FILE *in, FILE *out, int echo, struct charset *ct,
		    struct encoding *enc)
{
	char buf[19];
	struct header_lex lex;
	size_t n;
	int state;

	lex.cstate = 0;
	lex.echo = echo ? out : NULL;
	lex.qstate = 0;
	lex.skipws = 1;

	state = 0;
	n = 0;
	for (;;) {
		int ch;

		if ((ch = header_lex(in, &lex)) == HL_ERR)
			return -1;
		if (ch == HL_PIPE)
			return 0;
		if (ch == HL_EOF)
			break;

		if (state == 0) {
			if (ch == '/') {
				buf[n] = '\0';

				if (strcmp(buf, "text") != 0) {
					charset_from_type(ct, CHARSET_OTHER);
					encoding_from_type(enc, ENCODING_BINARY);
				}
				n = 0;
				state = 1;
				continue;
			}

			if (n == sizeof(buf) - 1) {
				charset_from_type(ct, CHARSET_OTHER);
				continue;
			}
			buf[n++] = ch;
		}

		if (state == 1) {
			if (ch == ';') {
				lex.skipws = 1;
				state = 2;
			}
			continue;
		}

		if (state == 2) {
			if (ch == '=') {
				buf[n] = '\0';

				if (!strcmp(buf, "charset"))
					state = 3;
				else
					state = 4;
				n = 0;
				continue;
			}

			if (n == sizeof(buf) - 1)
				continue;
			buf[n++] = ch;
		}

		if (state == 3) {
			if (ch == ';') {
				buf[n] = '\0';

				if (charset_from_name(ct, buf) == -1)
					charset_from_type(ct, CHARSET_OTHER);
				state = 2;
				n = 0;
			}

			if (n == sizeof(buf) - 1)
				continue;
			buf[n++] = ch;
		}

		if (state == 4) {
			if (ch == ';')
				state = 2;
			continue;
		}
	}

	if (state == 0)
		return -1;

	if (state == 3) {
		buf[n] = '\0';

		if (charset_from_name(ct, buf) == -1)
			charset_from_type(ct, CHARSET_OTHER);
	}

	return 1;
}

static int
header_encoding(FILE *in, FILE *out, int echo, struct encoding *e)
{
	struct header_lex lex;
	char buf[17];
	size_t n;
	int state;

	lex.cstate = 0;
	lex.echo = echo ? out : NULL;
	lex.qstate = 0;
	lex.skipws = 1;

	n = 0;
	state = 0;
	for (;;) {
		int ch;

		if ((ch = header_lex(in, &lex)) == HL_ERR)
			return -1;
		if (ch == HL_EOF)
			break;
		if (ch == HL_PIPE)
			return 0;

		if (state == 0) {
			if (n == sizeof(buf) - 1) {
				state = 1;
				continue;
			}
			buf[n++] = ch;
		}

		if (state == 1)
			continue;
	}

	if (state == 0) {
		buf[n] = '\0';

		if (encoding_from_name(e, buf) == -1)
			encoding_from_type(e, ENCODING_BINARY);
	}
	else
		encoding_from_type(e, ENCODING_BINARY);

	return 1;
}

static int
header_from(FILE *fp, struct from *from)
{
	struct header_lex lex;
	size_t n;
	int state;

	if (from->addr != NULL && from->addrsz == 0)
		return -1;
	if (from->name != NULL && from->namesz == 0)
		return -1;

	lex.cstate = 0;
	lex.echo = NULL;
	lex.qstate = 0;
	lex.skipws = 1;

	n = 0;
	state = 1;
	for (;;) {
		int ch;

		if ((ch = header_lex(fp, &lex)) == HL_ERR)
			return -1;
		if (ch == HL_EOF)
			break;

		if (state == 1) {
			if (from->addr != NULL) {
				if (ch == '<') {
					if (from->name != NULL) {
						if (n >= from->namesz)
							return -1;
						memcpy(from->name, from->addr, n);
						from->name[n] = '\0';
						strip_trailing(from->name);
					}

					from->addr[n] = '\0';
					state = 2;
					n = 0;
					continue;
				}

				if (n == from->addrsz - 1)
					return -1;
				from->addr[n++] = ch;
			}
			else if (from->name != NULL) {
				if (ch == '<') {
					state = 3;
					from->name[n] = '\0';
					strip_trailing(from->name);
					n = 0;
					continue;
				}
				if (n == from->namesz - 1) {
					state = 4;
					continue;
				}
				from->name[n++] = ch;
			}
			else
				return -1;
		}

		if (state == 2) {
			if (ch == '>') {
				if (from->addr != NULL)
					from->addr[n] = '\0';
				state = 5;
				continue;
			}
			if (from->addr != NULL) {
				if (n == from->addrsz - 1)
					return -1;
				from->addr[n++] = ch;
			}
		}

		if (state == 3)
			continue;

		if (state == 4) {
			if (ch == '<')
				state = 5;
			continue;
		}

		if (state == 5)
			continue;
	}

	if (state == 1) {
		if (from->addr != NULL)
			from->addr[n] = '\0';
		else if (from->name != NULL)
			from->name[0] = '\0';
	}

	if (state == 2)
		return -1;

	if (state == 4)
		return -1;

	return 0;
}

static int
header_lex(FILE *fp, struct header_lex *lex)
{
	for (;;) {
		int ch;

		if ((ch = fgetc(fp)) == EOF)
			goto eof;
		if (ch == '\n') {
			if ((ch = fgetc(fp)) == EOF)
				goto eof;
			if (ch != ' ' && ch != '\t') {
				if (ungetc(ch, fp) == EOF)
					return HL_ERR;
				goto eof;
			}
		}

		if (lex->echo != NULL) {
			if (fputc(ch, lex->echo) == EOF) {
				if (ferror(lex->echo) && errno == EPIPE)
					return HL_PIPE;
				return HL_ERR;
			}
		}

		if (lex->cstate != -1) {
			if (ch == '(') {
				if (lex->cstate == INT_MAX)
					return HL_ERR;
				lex->cstate++;
				continue;
			}

			if (lex->cstate > 0) {
				if (ch == ')')
					lex->cstate--;
				continue;
			}
		}

		if (lex->qstate != -1) {
			if (ch == '\"') {
				lex->qstate = !lex->qstate;
				continue;
			}
		}

		if (lex->skipws) {
			if (ch == ' ' || ch == '\t')
				continue;
			lex->skipws = 0;
		}

		return ch;
	}

	eof:
	if (lex->echo != NULL) {
		if (fputc('\n', lex->echo) == EOF) {
			if (ferror(lex->echo) && errno == EPIPE)
				return HL_PIPE;
			return HL_ERR;
		}
	}

	if (lex->cstate != -1 && lex->cstate != 0)
		return HL_ERR;
	if (lex->qstate != -1 && lex->qstate != 0)
		return HL_ERR;
	return HL_EOF;
}

static int
header_message_id(FILE *fp, char *buf, size_t bufsz)
{
	struct header_lex lex;
	size_t n;
	int state;

	if (bufsz == 0)
		return -1;

	lex.cstate = 0;
	lex.echo = NULL;
	lex.qstate = 0;
	lex.skipws = 0;

	n = 0;
	state = 0;
	for (;;) {
		int ch;

		if ((ch = header_lex(fp, &lex)) == HL_ERR)
			return -1;
		if (ch == HL_EOF)
			break;

		if (state == 2)
			continue;

		if (state == 0) {
			if (ch == '<')
				state = 1;
			continue;
		}

		if (ch == '>') {
			state = 2;
			continue;
		}

		if (!isprint(ch) && !isspace(ch))
			return -1;
		if (n == bufsz - 1)
			return -1;
		buf[n++] = ch;
	}

	buf[n] = '\0';
	return 0;
}

static int
header_name(FILE *fp, char *buf, size_t bufsz)
{
	size_t n;

	if (bufsz == 0)
		return -1;

	n = 0;
	for (;;) {
		int ch;

		if ((ch = fgetc(fp)) == EOF)
			return -1;
		if (ch == ':')
			break;
		if (ch == '\n' && n == 0)
			return 0;

		if (ch < 33 || ch > 126)
			return -1;

		if (n == bufsz - 1)
			return -1;
		buf[n++] = ch;
	}

	buf[n] = '\0';
	return 1;
}

static int
header_references(FILE *fp, struct imsgbuf *msgbuf)
{
	struct header_lex lex;
	char buf[CNT_MSGID_LEN];
	size_t n;
	int state;

	lex.cstate = 0;
	lex.echo = NULL;
	lex.qstate = 0;
	lex.skipws = 1;

	n = 0;
	state = 0;
	for (;;) {
		int ch;

		if ((ch = header_lex(fp, &lex)) == HL_ERR)
			return -1;
		if (ch == HL_EOF)
			break;

		if (state == 0) {
			if (ch == '<')
				state = 1;
			continue;
		}

		if (ch == '>') {
			memset(&buf[n], 0, sizeof(buf) - n);
			if (imsg_compose(msgbuf,
					 IMSG_CNT_REFERENCE,
					 0, -1, -1,
					 buf, sizeof(buf)) == -1)
				return -1;
			state = 0;
			n = 0;
			continue;
		}

		if (!isprint(ch) && !isspace(ch))
			return -1;

		if (n == sizeof(buf) - 1)
			return -1;
		buf[n++] = ch;
	}

	if (state != 0)
		return -1;
	return 0;
}

static int
header_skip(FILE *in, FILE *out, int echo)
{
	struct header_lex lex;
	int ch;

	lex.cstate = -1;
	lex.echo = echo ? out : NULL;
	lex.qstate = -1;
	lex.skipws = 0;

	while ((ch = header_lex(in, &lex)) != HL_EOF) {
		if (ch == HL_ERR)
			return -1;
		if (echo && ch == HL_PIPE)
			return 0;
	}
	return 1;
}

static int
header_subject(FILE *fp, char *buf, size_t bufsz)
{
	struct header_lex lex;
	size_t n;
	int ch;

	if (bufsz == 0)
		return -1;

	lex.cstate = -1;
	lex.echo = NULL;
	lex.qstate = -1;
	lex.skipws = 1;

	n = 0;
	while ((ch = header_lex(fp, &lex)) != HL_EOF) {
		if (ch == HL_ERR)
			return -1;
		if (!isspace(ch) && !isprint(ch))
			continue;

		if (n == bufsz - 1)
			continue; /* truncate */
		buf[n++] = ch;
	}

	buf[n] = '\0';
	return 0;
}

static int
ignore_header(const char *name, struct ignore *ignore)
{
	size_t i;
	int rv;

	if (ignore->type == IGNORE_RETAIN)
		rv = 0;
	else
		rv = 1;

	for (i = 0; i < ignore->nheader; i++)
		if (!strcasecmp(name, ignore->headers[i]))
			return rv;
	return !rv;
}

static FILE *
imsg_get_fp(struct imsg *msg, const char *perm)
{
	FILE *rv;
	int fd;

	if ((fd = imsg_get_fd(msg)) == -1)
		return NULL;

	if ((rv = fdopen(fd, perm)) == NULL) {
		close(fd);
		return NULL;
	}

	return rv;
}

static void
strip_trailing(char *s)
{
	char *p;
	size_t len;

	len = strlen(s);
	if (len == 0)
		return;

	p = s + (len - 1);
	while (p > s && (*p == ' ' || *p == '\t'))
		*p-- = '\0';
}

static void
usage(void)
{
	fprintf(stderr, "usage: mailz-content\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	struct ignore ignore;
	struct imsgbuf msgbuf;
	size_t i;
	int ch, reexec;

	reexec = 0;
	while ((ch = getopt(argc, argv, "r")) != -1) {
		switch (ch) {
		case 'r':
			reexec = 1;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (!reexec)
		errx(1, "mailz-content should not be executed directly");

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		errx(1, "setlocale");
	if (pledge("stdio recvfd", NULL) == -1)
		err(1, "pledge");

	memset(&ignore, 0, sizeof(ignore));
	imsg_init(&msgbuf, CNT_PFD);
	for (;;) {
		struct imsg msg;
		ssize_t n;
		int hv;

		if ((n = imsg_get_blocking(&msgbuf, &msg)) == -1)
			goto msgbuf;
		if (n == 0)
			break;

		switch (imsg_get_type(&msg)) {
		case IMSG_CNT_IGNORE:
			hv = handle_ignore(&msg, &ignore, IGNORE_IGNORE);
			break;
		case IMSG_CNT_LETTER:
			hv = handle_letter(&msgbuf, &msg, &ignore);
			break;
		case IMSG_CNT_REPLY:
			hv = handle_reply(&msgbuf, &msg);
			break;
		case IMSG_CNT_RETAIN:
			hv = handle_ignore(&msg, &ignore, IGNORE_RETAIN);
			break;
		case IMSG_CNT_SUMMARY:
			hv = handle_summary(&msgbuf, &msg);
			break;
		default:
			hv = -1;
			break;
		}

		imsg_free(&msg);
		if (hv == -1)
			goto msgbuf;
	}

	msgbuf:
	imsg_clear(&msgbuf);
	for (i = 0; i < ignore.nheader; i++)
		free(ignore.headers[i]);
	free(ignore.headers);
	close(CNT_PFD);
}
