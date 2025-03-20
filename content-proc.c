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
#include <fcntl.h>
#include <imsg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "_err.h"
#include "content.h"
#include "content-proc.h"
#include "imsg-blocking.h"
#include "printable.h"

void
content_letter_close(struct content_letter *letter)
{
	fclose(letter->fp);
}

int
content_letter_finish(struct content_letter *letter)
{
	struct imsg msg;
	ssize_t n;
	int ch, rv;

	while ((ch = fgetc(letter->fp)) != EOF)
		;

	n = imsg_get_blocking(&letter->pr->msgbuf, &msg);
	if (n <= 0)
		return -1;

	rv = imsg_get_type(&msg) == IMSG_CNT_OK ? 0 : -1;
	imsg_free(&msg);
	return rv;
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
		    struct content_letter *letter, int fd)
{
	int p[2];

	if (pipe2(p, O_CLOEXEC) == -1)
		goto fd;

	if (imsg_compose(&pr->msgbuf, IMSG_CNT_LETTER, 0, -1, fd,
			 NULL, 0) == -1)
		goto p;
	fd = -1;

	if (imsg_compose(&pr->msgbuf, IMSG_CNT_LETTERPIPE, 0, -1,
			 p[1], NULL, 0) == -1)
		goto p;
	p[1] = -1;

	if (imsgbuf_flush(&pr->msgbuf) == -1)
		goto p;

	if ((letter->fp = fdopen(p[0], "r")) == NULL)
		goto p;
	memset(&letter->mbs, 0, sizeof(letter->mbs));
	letter->pr = pr;
	return 0;

	p:
	close(p[0]);
	if (p[1] != -1)
		close(p[1]);
	fd:
	if (fd != -1)
		close(fd);
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

	if (imsgbuf_flush(&pr->msgbuf) == -1)
		return -1;

	return 0;
}

int
content_proc_init(struct content_proc *pr, const char *exe, int null)
{
	int i, sv[2];

	if (socketpair(AF_UNIX,
		       SOCK_STREAM | SOCK_CLOEXEC, PF_UNSPEC,
		       sv) == -1)
		return -1;
	if (imsgbuf_init(&pr->msgbuf, sv[0]) == -1)
		goto sv;
	imsgbuf_allow_fdpass(&pr->msgbuf);

	switch (pr->pid = fork()) {
	case -1:
		goto msgbuf;
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
	return 0;

	msgbuf:
	imsgbuf_clear(&pr->msgbuf);
	sv:
	close(sv[0]);
	close(sv[1]);
	return -1;
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
content_proc_reply(struct content_proc *pr, FILE *out,
		   const char *from, int group, int lfd)
{
	struct imsg msg;
	mbstate_t mbs;
	struct content_reply_setup setup;
	FILE *in;
	ssize_t n;
	int i, p[2], rv;

	rv = -1;

	memset(&setup, 0, sizeof(setup));
	if (strlcpy(setup.addr, from, sizeof(setup.addr))
		    >= sizeof(setup.addr))
		goto lfd;
	setup.group = group;
	if (imsg_compose(&pr->msgbuf, IMSG_CNT_REPLY, 0, -1, lfd,
			 &setup, sizeof(setup)) == -1)
		goto lfd;

	if (pipe2(p, O_CLOEXEC) == -1)
		return -1;
	if ((in = fdopen(p[0], "r")) == NULL) {
		close(p[0]);
		close(p[1]);
		return -1;
	}

	if (imsg_compose(&pr->msgbuf, IMSG_CNT_REPLYPIPE, 0, -1, p[1],
			 NULL, 0) == -1) {
		close(p[1]);
		fclose(in);
		return -1;
	}

	if (imsgbuf_flush(&pr->msgbuf) == -1)
		goto in;

	i = 0;
	memset(&mbs, 0, sizeof(mbs));
	for (;;) {
		int ch;
		char cc;

		if ((ch = fgetc(in)) == EOF) {
			if (i == 0)
				break;
			goto in;
		}

		cc = ch;
		switch (mbrtowc(NULL, &cc, 1, &mbs)) {
		case -1:
		case -3:
		case 0:
			goto in;
		case -2:
			i++;
			break;
		default:
			if (i == 0 && !isprint(ch) && !isspace(ch))
				goto in;
			i = 0;
			break;
		}

		if (fputc(ch, out) == EOF)
			goto in;
	}

	if ((n = imsg_get_blocking(&pr->msgbuf, &msg)) == -1)
		goto in;
	if (n == 0)
		goto in;
	if (imsg_get_type(&msg) != IMSG_CNT_REPLY)
		goto msg;

	rv = 0;
	msg:
	imsg_free(&msg);
	in:
	fclose(in);
	return rv;

	lfd:
	close(lfd);
	return -1;
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

	if (imsgbuf_flush(&pr->msgbuf) == -1)
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

	if (!string_printable(sm->from, sizeof(sm->from)))
		goto bad;

	if (!string_printable(sm->subject, sizeof(sm->subject)))
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
