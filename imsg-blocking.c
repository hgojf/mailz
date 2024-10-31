#include <sys/queue.h>

#include <errno.h>
#include <imsg.h>
#include <poll.h>

#include "imsg-blocking.h"

int
imsg_flush_blocking(struct imsgbuf *msgbuf)
{
	struct pollfd pollfd;
	int n;

	pollfd.fd = msgbuf->fd;
	pollfd.events = POLLOUT;

	while ((n = imsg_flush(msgbuf)) == -1 && errno == EAGAIN) {
		if (poll(&pollfd, 1, -1) == -1)
			return -1;
		if (pollfd.revents & (POLLERR | POLLNVAL | POLLHUP))
			return -1;
	}

	return n;
}

ssize_t
imsg_get_blocking(struct imsgbuf *msgbuf, struct imsg *msg)
{
	struct pollfd pollfd;
	ssize_t n;

	pollfd.fd = msgbuf->fd;
	pollfd.events = POLLIN;

	while ((n = imsg_get(msgbuf, msg)) == 0) {
		if ((n = imsg_read(msgbuf)) == -1) {
			if (errno != EAGAIN)
				return -1;
			if (poll(&pollfd, 1, -1) == -1)
				return -1;
			if (pollfd.revents & (POLLERR | POLLNVAL))
				return -1;
		}

		if (n == 0)
			return 0;
	}

	return n;
}
