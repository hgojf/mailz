#include <sys/queue.h>

#include <errno.h>
#include <imsg.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>

#include "imsg-util.h"

int
ibuf_get_string(struct ibuf *ibuf, char **out)
{
	char *data, *e, *s;
	size_t len, sz;

	data = ibuf_data(ibuf);
	sz = ibuf_size(ibuf);

	if ((e = memchr(data, '\0', sz)) == NULL) {
		errno = EBADMSG;
		return -1;
	}

	if ((len = e - data) == 0) {
		/* empty */
		if (ibuf_skip(ibuf, 1) == -1)
			return -1;
		*out = NULL;
		return -2;
	}

	if ((s = malloc(len + 1)) == NULL)
		return -1;
	if (ibuf_get(ibuf, s, len + 1) == -1) {
		free(s);
		return -1;
	}

	*out = s;
	return len;
}


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
//	if (pollfd.revents & POLLHUP)
//		return 0;

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

