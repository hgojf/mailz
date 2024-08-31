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
