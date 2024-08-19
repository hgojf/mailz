#include <sys/queue.h>
#include <sys/uio.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <imsg.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "header.h"
#include "leak.h"
#include "maildir-read.h"
#include "util.h"

struct from {
	char *addr;
	char *name;
};

struct letter {
	time_t date;
	struct from from;
	char *message_id;
	char *subject;
};

enum header_ident {
	HEADER_DATE,
	HEADER_FROM,
	HEADER_MESSAGE_ID,
	HEADER_SUBJECT,
	HEADER_UNKNOWN,
};

static time_t date_parse(char *);
static int from_parse(char *, struct from *);
static enum header_ident header_ident(const char *);
static int letter_compose(struct imsgbuf *, struct letter *);
static int letter_read(FILE *, struct getline *, struct letter *);
static void letter_free(struct letter *);

#define PARENT_FD 3

int
main(int argc, char *argv[])
{
	struct getline gl;
	struct imsgbuf msgbuf;
	struct pollfd pollfd;

	if (pledge("stdio recvfd", NULL) == -1)
		err(1, "pledge");

	imsg_init(&msgbuf, PARENT_FD);

	pollfd.fd = PARENT_FD;
	pollfd.events = POLLIN;

	memset(&gl, 0, sizeof(gl));
	for (;;) {
		ssize_t n;

		if (poll(&pollfd, 1, -1) == -1)
			err(1, "poll");

		if (pollfd.revents & POLLERR)
			errx(1, "socket error");
		if (pollfd.revents & POLLHUP)
			goto eof;
		if (pollfd.revents & POLLNVAL)
			errc(1, EBADF, "poll");

		if (pollfd.revents & POLLOUT) {
			while (msgbuf_queuelen(&msgbuf.w) != 0) {
				if ((n = msgbuf_write(&msgbuf.w)) == -1) {
					if (errno == EAGAIN)
						goto read;
					err(1, "msgbuf_write");
				}
				if (n == 0)
					errx(1, "parent hung up with messages queued");
			}

			/* all messages sent */
			pollfd.events &= ~POLLOUT;
		}

		read:
		if (!(pollfd.revents & POLLIN))
			continue;

		if ((n = imsg_read(&msgbuf)) == -1) {
			if (errno == EAGAIN)
				continue;
			err(1, "imsg_read");
		}
		if (n == 0)
			goto eof;

		for (;;) {
			struct imsg msg;

			if ((n = imsg_get(&msgbuf, &msg)) == -1)
				err(1, "imsg_get");
			if (n == 0)
				break;

			switch (imsg_get_type(&msg)) {
			case IMSG_MDR_FILE: {
				struct letter letter;
				FILE *fp;
				int fd;

				if ((fd = imsg_get_fd(&msg)) == -1)
					errx(1, "parent sent bogus imsg (missing fd)");
				if ((fp = fdopen(fd, "r")) == NULL)
					err(1, "fdopen");

				if (letter_read(fp, &gl, &letter) == -1)
					errx(1, "failed to read header");

				if (letter_compose(&msgbuf, &letter) == -1)
					err(1, "letter_compose");

				fclose(fp);
				letter_free(&letter);
				pollfd.events |= POLLOUT;
				break;
			}
			default:
				errx(1, "parent sent bogus imsg");
			}

			imsg_free(&msg);
		}

		continue;

		eof:
		if (msgbuf_queuelen(&msgbuf.w) != 0)
			errx(1, "parent hung up with messages queued");
		break;
	}

	free(gl.line);
	imsg_clear(&msgbuf);
	close(PARENT_FD);

	if (fd_leak_report())
		exit(1);
	return 0;
}

static void
letter_free(struct letter *letter)
{
	free(letter->from.addr);
	free(letter->from.name);
	free(letter->message_id);
	free(letter->subject);
}

static int
letter_read(FILE *fp, struct getline *gl, struct letter *out)
{
	time_t date;
	struct from from;
	char *message_id, *subject;

	date = -1;
	memset(&from, 0, sizeof(from));
	message_id = NULL;
	subject = NULL;

	for (;;) {
		struct header header;
		int hv;

		if ((hv = header_read(fp, gl, &header, 1)) == HEADER_EOF)
			break;
		if (hv == HEADER_ERR) {
			warnx("failed to read header");
			goto fail;
		}

		switch (header_ident(header.key)) {
		case HEADER_DATE:
			if (date != -1) {
				warnx("more than one Date header in email");
				goto header_fail;
			}

			if ((date = date_parse(header.val)) == -1)
				goto header_fail;
			free(header.key);
			free(header.val);
			break;
		case HEADER_FROM:
			if (from.addr != NULL) {
				warnx("more than one From header in email");
				goto header_fail;
			}
			if (from_parse(header.val, &from) == -1) {
				warnx("failed to parse From address");
				goto header_fail;
			}
			free(header.key);
			break;
		case HEADER_MESSAGE_ID:
			if (message_id != NULL) {
				warnx("more than one Message-ID in email");
				goto header_fail;
			}
			free(header.key);
			message_id = header.val;
			break;
		case HEADER_SUBJECT:
			if (subject != NULL) {
				warnx("more than one Subject in email");
				goto header_fail;
			}
			free(header.key);
			subject = header.val;
			break;
		case HEADER_UNKNOWN:
			free(header.key);
			free(header.val);
			break;
		default:
			/* NOTREACHED */
			abort();
		}

		continue;

		header_fail:
		free(header.key);
		free(header.val);
		goto fail;
	}

	if (date == -1) {
		warnx("no Date header");
		goto fail;
	}

	if (from.addr == NULL) {
		warnx("no From header");
		goto fail;
	}

	/* from.name, message_id, and subject are optional. */

	out->date = date;
	out->from = from;
	out->message_id = message_id;
	out->subject = subject;
	return 0;

	fail:
	free(from.addr);
	free(from.name);
	free(message_id);
	free(subject);
	return -1;
}

static int
letter_compose(struct imsgbuf *msgbuf, struct letter *letter)
{
	#define iov_push(iov, d, l) do { \
			iov[ioc].iov_base = d; \
			iov[ioc].iov_len = l; \
			ioc++; \
		} while (0)
	#define iov_push_optstr(iov, s) do { \
			if (s != NULL) \
				iov_push(iov, s, strlen(s) + 1); \
			else \
				iov_push(iov, "", 1); \
		} while (0)

	struct iovec iov[5];
	int ioc;

	ioc = 0;

	iov_push(iov, &letter->date, sizeof(letter->date));
	iov_push(iov, letter->from.addr, strlen(letter->from.addr) + 1);
	iov_push_optstr(iov, letter->from.name);
	iov_push_optstr(iov, letter->message_id);
	iov_push_optstr(iov, letter->subject);

	return imsg_composev(msgbuf, IMSG_MDR_LETTER, 0, -1, -1, iov, ioc);
	#undef iov_push
	#undef iov_push_optstr
}

static time_t
date_parse(char *s)
{
	struct tm tm;
	char *b;
	const char *fmt;
	time_t date;
	long off;

	if ((b = strchr(s, '(')) != NULL)
		*b = '\0';

	if (strchr(s, ',') != NULL)
		fmt = "%a, %d %b %Y %H:%M:%S %z";
	else
		fmt = "%d %b %Y %H:%M:%S %z";

	memset(&tm, 0, sizeof(tm));
	if (strptime(s, fmt, &tm) == NULL) {
		warnx("invalid Date header");
		return -1;
	}

	off = tm.tm_gmtoff;
	if ((date = timegm(&tm)) == -1) {
		warn("timegm");
		return -1;
	}

	date -= off;

	return date;
}

/* takes ownership of s on success */
static int
from_parse(char *s, struct from *out)
{
	char *addr;

	if ((addr = strchr(s, '<')) != NULL) {
		size_t al;

		addr++;
		al = strlen(addr);

		if (addr[al - 1] != '>') {
			warnx("address missing terminating '>'");
			return -1;
		}

		if ((out->addr = strndup(addr, al - 1)) == NULL) {
			warn(NULL);
			return -1;
		}

		addr[-1] = '\0';

		strip_trailing(s);
		out->name = s;
	}
	else {
		out->addr = s;
		out->name = NULL;
	}

	return 0;
}

static enum header_ident
header_ident(const char *s)
{
	switch (tolower((unsigned char)s[0])) {
	case 'd':
		if (!strcasecmp(&s[1], "ate"))
			return HEADER_DATE;
		break;
	case 'f':
		if (!strcasecmp(&s[1], "rom"))
			return HEADER_FROM;
		break;
	case 'm':
		if (!strcasecmp(&s[1], "essage-id"))
			return HEADER_MESSAGE_ID;
		break;
	case 's':
		if (!strcasecmp(&s[1], "ubject"))
			return HEADER_SUBJECT;
		break;
	default:
		break;
	}

	return HEADER_UNKNOWN;
}
