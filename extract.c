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
#include <sys/uio.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <imsg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "extract.h"
#include "ibuf-util.h"
#include "imsg-sync.h"
#include "maildir-extract.h"
#include "pathnames.h"
#include "printable.h"
#include "util.h"

void
extract_header_free(enum extract_header_type type,
	union extract_header_val *val)
{
	switch (type) {
	case EXTRACT_DATE:
		break;
	case EXTRACT_FROM:
		free(val->from.addr);
		free(val->from.name);
		break;
	case EXTRACT_MESSAGE_ID:
	case EXTRACT_STRING:
		free(val->string);
		break;
	}
}

static int
maildir_extract1(struct extract *extract)
{
	FILE *e;
	int p[2], sv[2];
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

	switch (pid = fork()) {
	case -1:
		goto p;
	case 0:
		if (dup2(p[1], STDERR_FILENO) == -1)
			err(1, "dup2");
		if (dup2(sv[1], 3) == -1)
			err(1, "dup2");
		execl(PATH_MAILDIR_EXTRACT, "maildir-extract", NULL);
		err(1, "%s", PATH_MAILDIR_EXTRACT);
	default:
		break;
	}

	extract->pid = pid;

	close(sv[1]);
	imsg_init(&extract->msgbuf, sv[0]);
	extract->fd = sv[0];

	close(p[1]);
	extract->e = e;

	return 0;

	p:
	fclose(e);
	close(p[1]);
	sv:
	close(sv[1]);
	close(sv[0]);
	return -1;
}

int
maildir_extract(struct extract *extract, struct extracted_header *headers, 
	size_t nh)
{
	if (maildir_extract1(extract) == -1)
		return -1;

	for (size_t i = 0; i < nh; i++) {
		struct iovec iov[2];

		iov[0].iov_base = &headers[i].type;
		iov[0].iov_len = sizeof(headers[i].type);

		iov[1].iov_base = headers[i].key;
		iov[1].iov_len = strlen(headers[i].key);

		if (imsg_composev(&extract->msgbuf, IMSG_MDE_HEADERDEF, 0, 
			-1, -1, iov, nitems(iov)) == -1)
				goto extract;
	}

	if (imsg_flush_blocking(&extract->msgbuf) == -1)
		goto extract;

	return 0;

	extract:
	maildir_extract_close(extract);
	return -1;
}

int
maildir_extract_close(struct extract *extract)
{
	int rv, status;

	imsg_clear(&extract->msgbuf);
	close(extract->fd);

	if (waitpid(extract->pid, &status, 0) == -1)
		rv = -1;
	else if (WEXITSTATUS(status) != 0) {
		int any, ch;

		any = 0;
		while ((ch = fgetc(extract->e)) != EOF) {
			if (!isprint(ch) && !isspace(ch))
				fprintf(stderr, "%hhx", ch);
			else
				fputc(ch, stderr);
			any = 1;
		}

		if (!any)
			warnx("maildir-extract failed without error message");
		rv = -1;
	}
	else
		rv = 0;

	fclose(extract->e);

	return rv;
}

/* 
 * This function takes ownership of fd.
 * The headers and nh arguments must be the same as what was passed
 * to maildir_extract, or results will be wrong.
 */
int
maildir_extract_next(struct extract *extract, 
	int fd, struct extracted_header *headers, size_t nh)
{
	if (imsg_compose(&extract->msgbuf, IMSG_MDE_LETTER, 0, -1, fd, 
		NULL, 0) == -1) {
			close(fd);
			return -1;
	}

	if (imsg_flush_blocking(&extract->msgbuf) == -1)
		return -1;

	for (size_t i = 0; i < nh; i++) {
		switch (headers[i].type) {
		case EXTRACT_DATE:
			headers[i].val.date = -1;
			break;
		case EXTRACT_FROM:
			memset(&headers[i].val.from, 0, 
				sizeof(headers[i].val.from));
			break;
		case EXTRACT_MESSAGE_ID:
		case EXTRACT_STRING:
			headers[i].val.string = NULL;
			break;
		default:
			warnx("invalid header type");
			return -1;
		}
	}

	for (;;) {
		struct ibuf ibuf;
		struct imsg msg;
		size_t i;

		if (imsg_get_blocking(&extract->msgbuf, &msg) <= 0)
			goto headers;

		if (imsg_get_type(&msg) == IMSG_MDE_HEADERDONE) {
			imsg_free(&msg);
			break;
		}

		if (imsg_get_type(&msg) != IMSG_MDE_HEADER)
			goto msg;

		if (imsg_get_ibuf(&msg, &ibuf) == -1)
			goto msg;

		if (ibuf_get(&ibuf, &i, sizeof(i)) == -1)
			goto msg;

		if (i >= nh)
			goto msg;

		switch (headers[i].type) {
		case EXTRACT_DATE:
			if (headers[i].val.date != -1)
				goto msg;
			if (ibuf_get(&ibuf, &headers[i].val.date, 
				sizeof(headers[i].val.date)) == -1)
					goto msg;
			if (ibuf_size(&ibuf) != 0)
				goto msg;

			/* avoid errors later */
			if (localtime(&headers[i].val.date) == NULL)
				goto msg;
			break;
		case EXTRACT_FROM:
			if (headers[i].val.from.addr != NULL)
				goto msg;
			if (ibuf_get_delim(&ibuf, &headers[i].val.from.addr, 
				'\0') == -1)
					goto msg;
			if (!string_isprint(headers[i].val.from.addr))
				goto msg;

			if (ibuf_size(&ibuf) != 0) {
				if (ibuf_get_string(&ibuf, &headers[i].val.from.name, 
					ibuf_size(&ibuf)) == -1)
						goto msg;

				if (!string_isprint(headers[i].val.from.name))
					goto msg;
			}
			else
				headers[i].val.from.name = NULL;
			break;
		case EXTRACT_MESSAGE_ID:
		case EXTRACT_STRING:
			if (headers[i].val.string != NULL)
				goto msg;
			if (ibuf_get_string(&ibuf, &headers[i].val.string, 
				ibuf_size(&ibuf)) == -1)
				goto msg;

			if (!string_isprint(headers[i].val.string))
				goto msg;
			break;
		}

		imsg_free(&msg);

		continue;

		msg:
		imsg_free(&msg);
		goto headers;
	}

	return 0;
	headers:
	for (size_t i = 0; i < nh; i++)
		extract_header_free(headers[i].type, &headers[i].val);
	return -1;
}

int
maildir_extract_quick(int fd, struct extracted_header *headers, size_t nh)
{
	struct extract extract;
	int rv;

	rv = -1;

	if (maildir_extract(&extract, headers, nh) == -1) {
		close(fd);
		return -1;
	}

	if (maildir_extract_next(&extract, fd, headers, nh) == -1)
		goto extract;

	rv = 0;
	extract:
	if (maildir_extract_close(&extract) == -1)
		rv = -1;
	return rv;
}
