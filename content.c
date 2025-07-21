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
#include <fcntl.h>
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
#include "header.h"
#include "imsg-blocking.h"
#include "pathnames.h"

/*
 * Email lines should be at max 998 bytes, excluding the CRLF.
 * One byte is used for the ':', the rest are available for header
 * identifiers.
 * This length includes the terminating NUL byte.
 */
#define HEADER_NAME_LEN 998

/*
 * Email lines should be at max 998 bytes, excluding the CRLF.
 * One byte (at minimum) is used for the header identifier,
 * another is used for the ':', 2 are used for the opening and closing
 * '<'.
 * This length includes the terminating NUL byte.
 */
#define MSGID_LEN 995

struct ignore {
	char **headers;
	size_t nheader;
	#define IGNORE_IGNORE 0
	#define IGNORE_RETAIN 1
	int type;
};

static int handle_ignore(struct imsg *, struct ignore *, int);
static int handle_letter(struct imsgbuf *, struct imsg *, struct ignore *);
static int handle_letter_under(FILE *, FILE *, struct ignore *, int);
static int handle_reply(struct imsgbuf *, struct imsg *);
static int handle_reply_body(FILE *, FILE *, time_t, const char *,
			     const char *);
static int handle_reply_references(FILE *, FILE *, const char *,
				   const char *, off_t);
static int handle_reply_to(FILE *, FILE *, const char *, off_t, off_t, off_t);
static int handle_summary(struct imsgbuf *, struct imsg *);
static int ignore_header(const char *, struct ignore *);
static FILE *imsg_get_fp(struct imsg *, const char *);
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
	FILE *in, *out;
	ssize_t n;
	int rv;

	rv = -1;

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

	if (handle_letter_under(in, out, ignore, 0) == -1)
		goto in;

	if (imsg_compose(msgbuf, IMSG_CNT_OK, 0, -1, -1, NULL, 0) == -1)
		goto in;
	if (imsgbuf_flush(msgbuf) == -1)
		goto in;

	in:
	fclose(in);
	out:
	fclose(out);
	msg2:
	imsg_free(&msg2);
	return rv;
}

static int
handle_letter_under(FILE *in, FILE *out, struct ignore *ignore,
		    int reply)
{
	struct charset charset;
	struct encoding encoding;
	int got_content_type, got_encoding;

	charset_from_type(&charset, CHARSET_ASCII);
	encoding_from_type(&encoding, ENCODING_7BIT);
	got_content_type = 0;
	got_encoding = 0;
	for (;;) {
		char buf[HEADER_NAME_LEN];
		FILE *echo;
		int hv;

		if ((hv = header_name(in, buf, sizeof(buf))) == HEADER_EOF)
			break;
		if (hv != HEADER_OK)
			return -1;

		if (reply || (ignore != NULL && ignore_header(buf, ignore)))
			echo = NULL;
		else
			echo = out;

		if (echo) {
			if (fprintf(out, "%s:", buf) < 0)
				return -1;
		}

		if (!strcasecmp(buf, "content-transfer-encoding")) {
			char buf[17];
			int enc;

			if (got_encoding)
				return -1;
			hv = header_encoding(in, echo, buf,
						  sizeof(buf));
			if (hv == HEADER_OK) {
				if ((enc = encoding_from_name(buf)) == ENCODING_UNKNOWN)
					enc = ENCODING_BINARY;
				encoding_from_type(&encoding, enc);
			}	
			else if (hv == HEADER_TRUNC)
				encoding_from_type(&encoding, ENCODING_BINARY);
			else
				return -1;
			got_encoding = 1;
		}
		else if (!strcasecmp(buf, "content-type")) {
			struct content_type ct;
			struct content_type_var vt;
			char type[5], var[8], val[11];
			int eof;

			if (got_content_type)
				return -1;

			ct.type = type;
			ct.typesz = sizeof(type);
			ct.subtype = NULL;
			ct.subtypesz = 0;

			eof = 0;
			hv = header_content_type(in, echo, &ct, &eof);
			if (hv < 0)
				return -1;

			if (ct.type_trunc || strcasecmp(type, "text") != 0) {
				charset_from_type(&charset, CHARSET_OTHER);
				encoding_from_type(&encoding, ENCODING_BINARY);
			}

			vt.var = var;
			vt.varsz = sizeof(var);
			vt.val = val;
			vt.valsz = sizeof(val);
			for (;;) {
				hv = header_content_type_var(in, echo, &vt, &eof);
				if (hv == HEADER_EOF)
					break;
				if (hv < 0)
					return -1;

				if (!vt.var_trunc && !strcasecmp(var, "charset")) {
					int ctype;

					if (!vt.val_trunc
					    && (ctype = charset_from_name(val)) != CHARSET_UNKNOWN)
						charset_from_type(&charset, ctype);
					else
						charset_from_type(&charset, CHARSET_OTHER);
				}
			}

			got_content_type = 1;
		}
		else {
			if (header_skip(in, echo) < 0)
				return -1;
		}
	}

	if (reply) {
		if (fprintf(out, "> ") < 0)
			return -1;
	}
	else {
		if (fputc('\n', out) == EOF)
			return -1;
	}

	for (;;) {
		char buf[4];
		int n;

		if ((n = charset_getc(&charset, &encoding, in,
				      buf)) == -1)
			return -1;
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

		if (fwrite(buf, n, 1, out) != 1)
			return -1;

		if (reply && n == 1 && buf[0] == '\n')
			if (fprintf(out, "> ") < 0)
				return -1;
	}

	return 0;
}

static int
handle_reply(struct imsgbuf *msgbuf, struct imsg *msg)
{
	struct content_reply_setup setup;
	struct imsg msg2;
	FILE *in, *out;
	char addr_buf[255], *addr, in_reply_to[MSGID_LEN], msgid[MSGID_LEN];
	char from_addr[255], from_name[65];
	ssize_t n;
	time_t date;
	off_t from, references, reply_to, to;
	int got_subject, rv;

	rv = -1;

	if ((in = imsg_get_fp(msg, "r")) == NULL)
		return -1;

	if (imsg_get_data(msg, &setup, sizeof(setup)) == -1)
		goto in;
	if (memchr(setup.addr, '\0', sizeof(setup.addr)) == NULL)
		goto in;

	if ((n = imsg_get_blocking(msgbuf, &msg2)) == -1)
		goto in;
	if (n == 0)
		goto in;

	if (imsg_get_type(&msg2) != IMSG_CNT_REPLYPIPE)
		goto msg2;
	if ((out = imsg_get_fp(&msg2, "w")) == NULL)
		goto msg2;

	if ((addr = strchr(setup.addr, '<')) != NULL) {
		size_t n;

		addr++;
		n = strcspn(addr, ">");
		if (n >= sizeof(addr_buf))
			goto out;
		memcpy(addr_buf, addr, n);
		addr_buf[n] = '\0';
		addr = addr_buf;
	}
	else
		addr = setup.addr;

	date = -1;
	from = -1;
	got_subject = 0;
	in_reply_to[0] = '\0';
	msgid[0] = '\0';
	references = -1;
	reply_to = -1;
	to = -1;
	for (;;) {
		char buf[HEADER_NAME_LEN];
		int hv;

		if ((hv = header_name(in, buf, sizeof(buf))) == HEADER_EOF)
			break;
		if (hv != HEADER_OK)
			goto out;

		if (setup.group && !strcasecmp(buf, "cc")) {
			int any;

			if (fprintf(out, "Cc:") < 0)
				goto out;
			any = 0;
			if (header_copy_addresses(in, out, addr, &any) < 0)
				goto out;
			if (fprintf(out, "\n") < 0)
				goto out;
		}
		else if (!strcasecmp(buf, "date")) {
			if (date != -1)
				goto out;
			if (header_date(in, &date) != HEADER_OK)
				goto out;
		}
		else if (!strcasecmp(buf, "from")) {
			struct header_address from_p;

			if (from != -1)
				goto out;
			if ((from = ftello(in)) == -1)
				goto out;

			from_p.addr = from_addr;
			from_p.addrsz = sizeof(from_addr);

			from_p.name = from_name;
			from_p.namesz = sizeof(from_name);

			if (header_from(in, &from_p) < 0)
				goto out;
		}
		else if (!strcasecmp(buf, "in-reply-to")) {
			if (strlen(in_reply_to) != 0)
				goto out;
			if (header_message_id(in, in_reply_to,
					      sizeof(in_reply_to)) < 0)
				goto out;
		}
		else if (!strcasecmp(buf, "message-id")) {
			if (strlen(msgid) != 0)
				goto out;
			if (header_message_id(in, msgid,
					      sizeof(msgid)) < 0)
				goto out;
		}
		else if (!strcasecmp(buf, "references")) {
			if (references != -1)
				goto out;
			if ((references = ftello(in)) == -1)
				goto out;
			if (header_skip(in, NULL) < 0)
				goto out;
		}
		else if (!strcasecmp(buf, "reply-to")) {
			if (reply_to != -1)
				goto out;
			if ((reply_to = ftello(in)) == -1)
				goto out;
			if (header_skip(in, NULL) < 0)
				goto out;
		}
		else if (!strcasecmp(buf, "subject")) {
			if (got_subject)
				goto out;
			if (header_subject_reply(in, out) < 0)
				goto out;
			got_subject = 1;
		}
		else if (setup.group && !strcasecmp(buf, "to")) {
			if (to != -1)
				goto out;
			if ((to = ftello(in)) == -1)
				goto out;
			if (header_skip(in, NULL) < 0)
				goto out;
		}
		else {
			if (header_skip(in, NULL) < 0)
				goto out;
		}
	}

	if (date == -1 || from == -1)
		goto out;

	if (!got_subject)
		if (fprintf(out, "Subject: Re: No Subject\n") < 0)
			goto out;

	if (fprintf(out, "From: %s\n", setup.addr) < 0)
		goto out;

	if (handle_reply_to(in, out, addr, from, to, reply_to) == -1)
		goto out;
	if (handle_reply_references(in, out, msgid, in_reply_to,
				    references) == -1)
		goto out;

	if (fprintf(out, "Content-Transfer-Encoding: 8bit\n") < 0)
		goto out;
	if (fprintf(out, "Content-Type: text/plain; charset=utf-8\n") < 0)
		goto out;

	if (handle_reply_body(in, out, date, from_addr, from_name) == -1)
		goto out;

	if (imsg_compose(msgbuf, IMSG_CNT_REPLY, 0, -1, -1,
			 NULL, 0) == -1)
		goto out;
	if (imsgbuf_flush(msgbuf) == -1)
		goto out;

	rv = 0;
	out:
	fclose(out);
	msg2:
	imsg_free(&msg2);
	in:
	fclose(in);
	return rv;
}

static int
handle_reply_body(FILE *in, FILE *out, time_t date, const char *addr,
		  const char *name)
{
	char datebuf[39];
	struct tm tm;

	if (fprintf(out, "\n") < 0)
		return -1;

	if (localtime_r(&date, &tm) == NULL)
		return -1;

	if (strftime(datebuf, sizeof(datebuf),
		     "%a, %b %d, %Y at %H:%M:%S %p %z", &tm) == 0)
		return -1;

	if (strlen(name) != 0) {
		if (fprintf(out, "On %s, %s <%s> wrote:\n",
			    datebuf, name, addr) < 0)
			return -1;
	}
	else {
		if (fprintf(out, "On %s, %s wrote:\n",
			    datebuf, addr) < 0)
			return -1;
	}

	if (fseeko(in, 0, SEEK_SET) == -1)
		return -1;
	return handle_letter_under(in, out, NULL, 1);
}

static int
handle_reply_references(FILE *in, FILE *out, const char *msgid,
			const char *in_reply_to, off_t refs)
{
	int putref;

	if (strlen(msgid) != 0) {
		if (fprintf(out, "In-Reply-To: <%s>\n", msgid) < 0)
			return -1;
	}

	putref = 0;
	if (refs != -1) {
		if (fseeko(in, refs, SEEK_SET) == -1)
			return -1;
		if (fprintf(out, "References:") < 0)
			return -1;
		if (header_copy(in, out) < 0)
			return -1;
		putref = 1;
	}
	else if (strlen(in_reply_to) != 0) {
		if (fprintf(out, "References: <%s>", in_reply_to) < 0)
			return -1;
		putref = 1;
	}

	if (strlen(msgid) != 0) {
		if (!putref) {
			if (fprintf(out, "References:") < 0)
				return -1;
			putref = 1;
		}
		if (fprintf(out, " <%s>", msgid) < 0)
			return -1;
	}

	if (putref)
		if (fprintf(out, "\n") < 0)
			return -1;

	return 0;
}

static int
handle_reply_to(FILE *in, FILE *out, const char *addr, off_t from,
		off_t to, off_t reply_to)
{
	int any;

	if (fprintf(out, "To:") < 0)
		return -1;

	any = 0;
	if (reply_to != -1) {
		if (fseeko(in, reply_to, SEEK_SET) == -1)
			return -1;
		if (header_copy_addresses(in, out, addr, &any) < 0)
			return -1;
	}
	else {
		if (fseeko(in, from, SEEK_SET) == -1)
			return -1;
		if (header_copy_addresses(in, out, addr, &any) < 0)
			return -1;
	}

	if (to != -1) {
		if (fseeko(in, to, SEEK_SET) == -1)
			return -1;
		if (header_copy_addresses(in, out, addr, &any) < 0)
			return -1;
	}

	if (fprintf(out, "\n") < 0)
		return -1;
	return 0;
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

		if ((n = header_name(fp, buf, sizeof(buf))) == HEADER_EOF)
			break;
		if (n != HEADER_OK)
			goto fp;

		if (!strcasecmp(buf, "date")) {
			if (sm.date != -1)
				goto fp;
			if (header_date(fp, &sm.date) != HEADER_OK)
				goto fp;
		}
		else if (!strcasecmp(buf, "from")) {
			struct header_address from;

			if (strlen(sm.from) != 0)
				goto fp;

			from.addr = sm.from;
			from.addrsz = sizeof(sm.from);

			from.name = NULL;
			from.namesz = 0;

			if (header_from(fp, &from) < 0)
				goto fp;
			if (strlen(sm.from) == 0)
				goto fp;
		}
		else if (!strcasecmp(buf, "subject")) {
			if (sm.have_subject)
				goto fp;
			if (header_subject(fp, sm.subject,
					   sizeof(sm.subject)) < 0)
				goto fp;
			sm.have_subject = 1;
		}
		else {
			if (header_skip(fp, NULL) < 0)
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
	if (imsgbuf_flush(msgbuf) == -1)
		goto fp;

	rv = 0;
	fp:
	fclose(fp);
	return rv;
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
	int ch, null, reexec;

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
	/* No argv += optind; because we dont use argv. */

	if (argc != 0)
		usage();

	if (!reexec)
		errx(1, "mailz-content should not be executed directly");

	if ((null = open(PATH_DEV_NULL, O_RDWR)) == -1)
		err(1, "%s", PATH_DEV_NULL);
	for (i = 0; i < 3; i++)
		if (dup2(null, i) == -1)
			err(1, "dup2");

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		errx(1, "setlocale");
	if (pledge("stdio recvfd", NULL) == -1)
		err(1, "pledge");

	memset(&ignore, 0, sizeof(ignore));
	if (imsgbuf_init(&msgbuf, CNT_PFD) == -1)
		err(1, "imsgbuf_init");
	imsgbuf_allow_fdpass(&msgbuf);
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
	imsgbuf_clear(&msgbuf);
	for (i = 0; i < ignore.nheader; i++)
		free(ignore.headers[i]);
	free(ignore.headers);
	close(CNT_PFD);
}
