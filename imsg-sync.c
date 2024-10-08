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

#include <errno.h>
#include <imsg.h>
#include <poll.h>

#include "imsg-sync.h"

int
imsg_flush_blocking(struct imsgbuf *msgbuf)
{
	struct pollfd pollfd;
	ssize_t n;

	pollfd.fd = msgbuf->w.fd;
	pollfd.events = POLLOUT;

	while (msgbuf_queuelen(&msgbuf->w) != 0) {
		if (poll(&pollfd, 1, -1) == -1)
			return -1;
		if (pollfd.revents & POLLHUP)
			return -1;

		n = imsg_flush(msgbuf);
		if (n == -1) {
			if (errno == EAGAIN)
				continue;
			return -1;
		}
	}

	return 0;
}

ssize_t
imsg_get_blocking(struct imsgbuf *msgbuf, struct imsg *msg)
{
	struct pollfd pollfd;
	ssize_t n;

	pollfd.fd = msgbuf->fd;
	pollfd.events = POLLIN;

	again:
	if ((n = imsg_get(msgbuf, msg)) == -1)
		return -1;
	if (n != 0)
		return n;

	poll:
	if (poll(&pollfd, 1, -1) == -1)
		return -1;

	n = imsg_read(msgbuf);
	if (n == -1) {
		if (errno == EAGAIN)
			goto poll;
		return -1;
	}
	if (n == 0)
		return 0;
	goto again;
}
