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
#include <sys/socket.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <imsg.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "content.h"
#include "content-proc.h"
#include "imsg-blocking.h"

static __dead void _err(int, const char *, ...)
	__attribute__((__format__(printf, 2, 3)));
static int string_valid(const char *, size_t);

void
content_letter_close(struct content_letter *letter)
{
	fclose(letter->fp);
}

int
content_letter_getc(struct content_letter *letter, char buf[static 4])
{
	int i;

	for (i = 0; i < 4; i++) {
		char c;
		int ch;

		if ((ch = fgetc(letter->fp)) == EOF) {
			if (i == 0)
				return 0;
			return -1;
		}

		c = ch;
		switch (mbrtowc(NULL, &c, 1, &letter->mbs)) {
		case -1:
		case -3:
		case 0:
			return -1;
		case -2:
			buf[i] = ch;
			break;
		default:
			if (i == 0 && !isprint(ch) && !isspace(ch))
				return -1;
			/* FALLTHROUGH */
			buf[i] = ch;
			return i + 1;
		}
	}

	/* probably NOTREACHED */
	return -1;
}

int
content_letter_init(struct content_proc *pr,
		    struct content_letter *letter, int fd, int flags)
{
	int p[2];

	if (pipe2(p, O_CLOEXEC) == -1) {
		close(fd);
		return -1;
	}

	if (imsg_compose(&pr->msgbuf, IMSG_CNT_LETTER, 0, -1, fd,
			 &flags, sizeof(flags)) == -1)
		goto fd;

	if (imsg_compose(&pr->msgbuf, IMSG_CNT_LETTERPIPE, 0, -1,
			 p[1], NULL, 0) == -1)
		goto p1;

	if (imsgbuf_flush_blocking(&pr->msgbuf) == -1)
		goto p0;

	if ((letter->fp = fdopen(p[0], "r")) == NULL)
		goto p0;
	memset(&letter->mbs, 0, sizeof(letter->mbs));
	return 0;

	fd:
	close(fd);
	p1:
	close(p[1]);
	p0:
	close(p[0]);
	return -1;
}

int
content_proc_ignore(struct content_proc *pr, const char *s, int type)
{
	struct content_header hdr;
	uint32_t msg_type;

	switch (type) {
	case CNT_IGNORE_IGNORE:
		msg_type = IMSG_CNT_IGNORE;
		break;
	case CNT_IGNORE_RETAIN:
		msg_type = IMSG_CNT_RETAIN;
		break;
	default:
		return -1;
	}

	memset(&hdr, 0, sizeof(hdr));
	if (strlcpy(hdr.name, s, sizeof(hdr.name)) >= sizeof(hdr.name))
		return -1;

	if (imsg_compose(&pr->msgbuf, msg_type, 0, -1, -1,
			 &hdr, sizeof(hdr)) == -1)
		return -1;

	if (imsgbuf_flush_blocking(&pr->msgbuf) == -1)
		return -1;

	return 0;
}

int
content_proc_init(struct content_proc *pr, const char *exe, int null)
{
	pid_t pid;
	int i, sv[2];

	if (socketpair(AF_UNIX,
		       SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
		       PF_UNSPEC, sv) == -1)
		return -1;

	switch (pid = fork()) {
	case -1:
		close(sv[0]);
		close(sv[1]);
		return -1;
	case 0:
		for (i = 0; i < 3; i++)
			if (dup2(null, i) == -1)
				_err(1, "dup2");
		if (dup2(sv[1], CNT_PFD) == -1)
			_err(1, "dup2");
		execl(exe, "mailz-content", "-r", NULL);
		_err(1, "%s", exe);
	default:
		break;
	}

	close(sv[1]);

	if (imsgbuf_init(&pr->msgbuf, sv[0]) == -1) {
		close(sv[0]);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		return -1;
	}
	imsgbuf_allow_fdpass(&pr->msgbuf);
	pr->pid = pid;
	return 0;
}

int
content_proc_kill(struct content_proc *pr)
{
	int status;

	close(pr->msgbuf.fd);
	imsgbuf_clear(&pr->msgbuf);

	if (waitpid(pr->pid, &status, 0) == -1)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;
	return 0;
}

int
content_proc_summary(struct content_proc *pr,
		     struct content_summary *sm, int fd)
{
	struct imsg msg;
	struct tm tm;
	ssize_t n;
	int rv;

	rv = -1;

	if (imsg_compose(&pr->msgbuf, IMSG_CNT_SUMMARY, 0, -1, fd,
			 NULL, 0) == -1) {
		close(fd);
		return -1;
	}

	if (imsgbuf_flush_blocking(&pr->msgbuf) == -1)
		return -1;

	if ((n = imsg_get_blocking(&pr->msgbuf, &msg)) == -1)
		return -1;
	if (n == 0)
		return -1;

	if (imsg_get_type(&msg) != IMSG_CNT_SUMMARY)
		goto bad;
	if (imsg_get_data(&msg, sm, sizeof(*sm)) == -1)
		goto bad;

	if (localtime_r(&sm->date, &tm) == NULL)
		goto bad;

	if (!string_valid(sm->from, sizeof(sm->from)))
		goto bad;

	if (!string_valid(sm->subject, sizeof(sm->subject)))
		goto bad;

	switch (sm->have_subject) {
	case 0:
		if (strcmp(sm->subject, "") != 0)
			goto bad;
		break;
	case 1:
		break;
	default:
		goto bad;
	}

	rv = 0;
	bad:
	imsg_free(&msg);
	return rv;
}

int
content_reply_close(struct content_reply *rpl)
{
	if (!rpl->eof) {
		for (;;) {
			struct imsg msg;
			ssize_t n;

			if ((n = imsg_get_blocking(&rpl->pr->msgbuf, &msg)) == -1)
				return -1;
			if (n == 0)
				return -1;

			if (imsg_get_type(&msg) == IMSG_CNT_REFERENCEOVER) {
				imsg_free(&msg);
				break;
			}

			if (imsg_get_type(&msg) != IMSG_CNT_REFERENCE) {
				imsg_free(&msg);
				return -1;
			}

			imsg_free(&msg);
		}
	}

	return 0;
}

int
content_reply_init(struct content_proc *pr, struct content_reply *rpl,
		   struct content_reply_summary *sm, int fd)
{
	struct imsg msg;
	ssize_t n;
	int rv;

	rv = -1;

	if (imsg_compose(&pr->msgbuf, IMSG_CNT_REPLY, 0, -1, fd,
			 NULL, 0) == -1) {
		close(fd);
		return -1;
	}

	if (imsgbuf_flush_blocking(&pr->msgbuf) == -1)
		return -1;

	if ((n = imsg_get_blocking(&pr->msgbuf, &msg)) == -1)
		return -1;
	if (n == 0)
		return -1;

	if (imsg_get_type(&msg) != IMSG_CNT_REPLY)
		goto bad;
	if (imsg_get_data(&msg, sm, sizeof(*sm)) == -1)
		goto bad;

	if (!string_valid(sm->name, sizeof(sm->name)))
		goto bad;

	if (!string_valid(sm->reply_to.addr, sizeof(sm->reply_to.addr)))
		goto bad;
	if (!string_valid(sm->reply_to.name, sizeof(sm->reply_to.name)))
		goto bad;

	if (!string_valid(sm->message_id, sizeof(sm->message_id)))
		goto bad;

	if (!string_valid(sm->in_reply_to, sizeof(sm->in_reply_to)))
		goto bad;

	rpl->pr = pr;
	rpl->eof = 0;

	rv = 0;
	bad:
	imsg_free(&msg);
	return rv;
}

int
content_reply_reference(struct content_reply *rpl,
			struct content_reference *ref)
{
	struct imsg msg;
	ssize_t n;
	int rv;

	rv = -1;

	if ((n = imsg_get_blocking(&rpl->pr->msgbuf, &msg)) == -1)
		return -1;
	if (n == 0)
		return -1;

	if (imsg_get_type(&msg) == IMSG_CNT_REFERENCEOVER) {
		imsg_free(&msg);
		rpl->eof = 1;
		return 0;
	}

	if (imsg_get_type(&msg) != IMSG_CNT_REFERENCE)
		goto bad;
	if (imsg_get_data(&msg, ref->id, sizeof(ref->id)) == -1)
		goto bad;

	if (!string_valid(ref->id, sizeof(ref->id)))
		goto bad;

	rv = 1;
	bad:
	imsg_free(&msg);
	return rv;
}

static void
_err(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarn(fmt, ap);
	_exit(eval);
	/* NOTREACHED */
	va_end(ap);
}

/*
 * Check if a string is NUL-terminated, and if it is printable.
 */
static int
string_valid(const char *s, size_t sz)
{
	mbstate_t mbs;
	size_t len, n;

	if ((len = strnlen(s, sz)) == sz)
		return 0;
	len += 1; /* include the NUL */

	memset(&mbs, 0, sizeof(mbs));

	while ((n = mbrtowc(NULL, s, len, &mbs)) != 0) {
		int ch;

		switch (n) {
		case -1:
		case -3:
			return 0;
		case -2:
			s += 1;
			len -= 1;
			break;
		case 1:
			ch = (unsigned char)*s;
			if (!isprint(ch) && !isspace(ch))
				return 0;
			/* FALLTHROUGH */
		default:
			s += n;
			len -= n;
			break;
		}
	}

	return 1;
}
