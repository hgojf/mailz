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
#include <sys/uio.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <imsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "date.h"
#include "from.h"
#include "header.h"
#include "ibuf-util.h"
#include "imsg-sync.h"
#include "maildir-extract.h"
#include "macro.h"

struct header_def {
	char *key;
	enum extract_header_type type;
	int seen;
};

#define PARENT_FD 3

static void mde_process_letter(struct imsgbuf *, FILE *,
	struct getline *, struct header_def *, size_t);

int
main(int argc, char *argv[])
{
	struct getline gl;
	struct header_def *headers;
	struct imsgbuf msgbuf;
	size_t nheader;

	if (pledge("stdio recvfd", NULL) == -1)
		err(1, "pledge");
	if (getdtablecount() != 4)
		errx(1, "extra file descriptors open");

	memset(&gl, 0, sizeof(gl));
	headers = NULL;
	imsg_init(&msgbuf, PARENT_FD);
	nheader = 0;

	for (;;) {
		struct imsg msg;
		ssize_t n;

		if ((n = imsg_get_blocking(&msgbuf, &msg)) == -1)
			err(1, "imsg_get_blocking");
		if (n == 0)
			break;

		switch (imsg_get_type(&msg)) {
		case IMSG_MDE_HEADERDEF: {
			struct header_def header;
			struct ibuf ibuf;

			if (imsg_get_ibuf(&msg, &ibuf) == -1)
				errx(1, "parent sent bogus imsg");

			if (ibuf_get(&ibuf, &header.type, 
				sizeof(header.type)) == -1)
					errx(1, "parent sent bogus imsg");
			switch (header.type) {
			case EXTRACT_DATE:
			case EXTRACT_FROM:
			case EXTRACT_STRING:
				break;
			default:
				errx(1, "invalid header type");
			}

			if (ibuf_get_string(&ibuf, &header.key, ibuf_size(&ibuf)) == -1)
				err(1, "ibuf_get_string");
			header.seen = 0;

			headers = reallocarray(headers, nheader + 1, sizeof(*headers));
			if (headers == NULL)
				err(1, NULL);
			headers[nheader++] = header;

			imsg_free(&msg);
			break;
		}
		case IMSG_MDE_LETTER: {
			FILE *fp;
			int fd;

			if ((fd = imsg_get_fd(&msg)) == -1)
				errx(1, "parent sent bogus imsg");
			if ((fp = fdopen(fd, "r")) == NULL)
				err(1, "fdopen");

			mde_process_letter(&msgbuf, fp, &gl, headers, nheader);
			imsg_free(&msg);
			fclose(fp);
			break;
		}
		default:
			errx(1, "parent sent bogus imsg (bad type)");
		}
	}

	free(gl.line);
	for (size_t i = 0; i < nheader; i++)
		free(headers[i].key);
	free(headers);
	imsg_clear(&msgbuf);
	close(PARENT_FD);

	return 0;
}

static void
mde_process_letter(struct imsgbuf *msgbuf, FILE *fp,
	struct getline *gl, struct header_def *headers, size_t nh)
{
	size_t nseen;

	nseen = 0;
	for (size_t i = 0; i < nh; i++)
		headers[i].seen = 0;

	for (;;) {
		struct header header;

		switch (header_read(fp, gl, &header, 1)) {
		case HEADER_ERR:
			errx(1, "invalid header");
		case HEADER_EOF:
			goto done;
		default:
			break;
		}

		for (size_t i = 0; i < nh; i++) {
			if (headers[i].seen)
				continue;
			if (strcasecmp(headers[i].key, header.key) != 0)
				continue;

			switch (headers[i].type) {
			case EXTRACT_DATE: {
				struct iovec iov[2];
				time_t date;

				if ((date = date_parse(header.val)) == -1)
					errx(1, "invalid date");

				iov[0].iov_base = &i;
				iov[0].iov_len = sizeof(i);

				iov[1].iov_base = &date;
				iov[1].iov_len = sizeof(date);

				if (imsg_composev(msgbuf, IMSG_MDE_HEADER, 0, -1, -1, 
					iov, nitems(iov)) == -1)
						err(1, "imsg_composev");
				break;
			}
			case EXTRACT_FROM: {
				struct iovec iov[4];
				struct from from;
				int ioc;

				if (from_parse(header.val, &from) == -1)
					errx(1, "invalid from");

				iov[0].iov_base = &i;
				iov[0].iov_len = sizeof(i);

				iov[1].iov_base = from.addr;
				iov[1].iov_len = from.al;

				iov[2].iov_base = "";
				iov[2].iov_len = 1;

				if (from.nl != 0) {
					iov[3].iov_base = from.name;
					iov[3].iov_len = from.nl;

					ioc = 4;
				}
				else
					ioc = 3;

				if (imsg_composev(msgbuf, IMSG_MDE_HEADER, 0, -1, -1, 
					iov, ioc) == -1)
						err(1, "imsg_composev");

				break;
			}
			case EXTRACT_STRING: {
				struct iovec iov[2];

				iov[0].iov_base = &i;
				iov[0].iov_len = sizeof(i);

				iov[1].iov_base = header.val;
				iov[1].iov_len = strlen(header.val);

				if (imsg_composev(msgbuf, IMSG_MDE_HEADER, 0, -1, -1, 
					iov, nitems(iov)) == -1)
						err(1, "imsg_composev");
				break;
			}
			}

			headers[i].seen = 1;

			if (++nseen == nh) {
				free(header.key);
				free(header.val);
				goto done;
			}

			break;
		}

		free(header.key);
		free(header.val);
	}

	done:
	if (imsg_compose(msgbuf, IMSG_MDE_HEADERDONE, 0, -1, -1, NULL, 
		0) == -1)
			err(1, "imsg_compose");

	if (imsg_flush_blocking(msgbuf) == -1)
		err(1, "imsg_flush_blocking");
	return;
}
