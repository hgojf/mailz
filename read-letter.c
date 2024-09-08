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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "conf.h"
#include "imsg-sync.h"
#include "maildir-read-letter.h"
#include "pathnames.h"
#include "read-letter.h"

static int maildir_read_letter1(struct read_letter *, struct imsgbuf *, int);

static int
imsg_compose_argv(struct imsgbuf *msgbuf, uint32_t type, uint32_t id, 
	pid_t pid, char **argv, size_t argc)
{
	struct ibuf *ibuf;

	if ((ibuf = ibuf_dynamic(0, (size_t)-1)) == NULL)
		return -1;

	for (size_t i = 0; i < argc; i++)
		if (ibuf_add(ibuf, argv[i], strlen(argv[i]) + 1) == -1)
			goto ibuf;

	return imsg_compose_ibuf(msgbuf, type, id, pid, ibuf);

	ibuf:
	ibuf_free(ibuf);
	return -1;
}

int
maildir_read_letter_close(struct read_letter *rl)
{
	int rv, status;

	rv = 0;

	fclose(rl->o); /* send SIGPIPE */
	if (waitpid(rl->pid, &status, 0) == -1)
		rv = -1;
	else if (WEXITSTATUS(status) != 0 || WIFSIGNALED(status)) {
		int any, ch;

		any = 0;
		while ((ch = fgetc(rl->e)) != EOF) {
			if (!isprint(ch) && !isspace(ch))
				fprintf(stderr, "%hhx", ch);
			else
				fputc(ch, stderr);
			any = 1;
		}

		if (!any)
			warnx("maildir-read-letter failed with error message");
		rv = -1;
	}

	fclose(rl->e);

	return rv;
}

static int
maildir_read_letter1(struct read_letter *read, struct imsgbuf *msgbuf, 
	int fd)
{
	FILE *e, *o;
	int p[2], p1[2], sv[2];
	pid_t pid;

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
		PF_UNSPEC, sv) == -1)
			return -1;
	if (pipe2(p, O_CLOEXEC) == -1)
		goto sv;
	if ((e = fdopen(p[0], "r")) == NULL) {
		close(p[0]);
		close(p[1]);
		goto sv;
	}

	if (pipe2(p1, O_CLOEXEC) == -1)
		goto p;
	if ((o = fdopen(p1[0], "r")) == NULL) {
		close(p[0]);
		close(p[1]);
		goto p;
	}

	switch (pid = fork()) {
	case -1:
		goto p1;
	case 0:
		if (dup2(fd, STDIN_FILENO) == -1)
			err(1, "dup2");
		if (dup2(p1[1], STDOUT_FILENO) == -1)
			err(1, "dup2");
		if (dup2(p[1], STDERR_FILENO) == -1)
			err(1, "dup2");
		if (dup2(sv[1], 3) == -1)
			err(1, "dup2");
		execl(PATH_MAILDIR_READ_LETTER, "maildir-read-letter", NULL);
		err(1, "%s", PATH_MAILDIR_READ_LETTER);
	default:
		break;
	}

	read->pid = pid;

	close(sv[1]);
	imsg_init(msgbuf, sv[0]);

	close(p[1]);
	read->e = e;

	close(p1[1]);
	read->o = o;

	return sv[0];

	p1:
	fclose(o);
	close(p1[1]);
	p:
	fclose(e);
	close(p[1]);
	sv:
	close(sv[1]);
	close(sv[0]);
	return -1;
}

int
maildir_read_letter(struct read_letter *rl, int fd, int pipeok,
	int linewrap, struct ignore *ignore, struct reorder *reorder)
{
	struct imsgbuf msgbuf;
	int any, s;

	if ((s = maildir_read_letter1(rl, &msgbuf, fd)) == -1) {
		close(fd);
		return -1;
	}

	close(fd);

	any = 0;
	if (ignore->argc != 0) {
		uint32_t type;

		switch (ignore->type) {
		case IGNORE_IGNORE:
			type = IMSG_MDR_IGNORE;
			break;
		case IGNORE_RETAIN:
			type = IMSG_MDR_RETAIN;
			break;
		default:
			goto rl;
		}

		if (imsg_compose_argv(&msgbuf, type, 0, -1,
			ignore->argv, ignore->argc) == -1)
				goto rl;
		any = 1;
	}
	else if (ignore->type == IGNORE_ALL) {
		if (imsg_compose(&msgbuf, IMSG_MDR_IGNOREALL, 0, -1, -1, 
			NULL, 0) == -1)
				goto rl;
		any = 1;
	}

	if (linewrap != 0) {
		if (imsg_compose(&msgbuf, IMSG_MDR_LINEWRAP, 0, -1, -1, 
			&linewrap, sizeof(linewrap)) == -1)
				goto rl;
		any = 1;
	}

	if (reorder->argc != 0) {
		if (imsg_compose_argv(&msgbuf, IMSG_MDR_REORDER, 0, -1,
			reorder->argv, reorder->argc) == -1)
				goto rl;
		any = 1;
	}

	if (pipeok) {
		if (imsg_compose(&msgbuf, IMSG_MDR_PIPEOK, 0, -1, -1, 
			NULL, 0) == -1)
				goto rl;
		any = 1;
	}

	if (any && imsg_flush_blocking(&msgbuf) == -1)
		goto rl;

	close(s);
	imsg_clear(&msgbuf);
	return 0;

	rl:
	maildir_read_letter_close(rl);
	close(s);
	imsg_clear(&msgbuf);
	return -1;
}

int
maildir_read_letter_getc(struct read_letter *rl, char out[static 4])
{
	mbstate_t mbs;

	memset(&mbs, 0, sizeof(mbs));
	for (int i = 0; i < 4; i++) {
		int ch;

		if ((ch = fgetc(rl->o)) == EOF) {
			if (i == 0)
				return 0;
			return -1;
		}
		out[i] = ch;

		switch (mbrtowc(NULL, &out[i], 1, &mbs)) {
		case (size_t)-1:
		case (size_t)-3:
		case 0:
			return -1;
		case (size_t) -2:
			break;
		default:
			if (i == 0
				&& (!isascii(ch) || (!isprint(ch) && !isspace(ch))))
					return -1;
			return i + 1;
		}
	}

	return -1;
}
