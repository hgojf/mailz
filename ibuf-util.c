#include <sys/queue.h>

#include <imsg.h>
#include <stdlib.h>
#include <string.h>

#include "ibuf-util.h"

int
ibuf_get_delim(struct ibuf *ibuf, char **s, int c)
{
	char *data, *end;
	size_t len, sz;

	data = ibuf_data(ibuf);
	sz = ibuf_size(ibuf);

	if ((end = memchr(data, c, sz)) == NULL)
		return -1;
	len = (size_t)(end - data) + 1;

	if (ibuf_get_string(ibuf, s, len) == -1)
		return -1;

	return 0;
}

int
ibuf_get_string(struct ibuf *ibuf, char **s, size_t n)
{
	char *rv;

	if ((rv = malloc(n + 1)) == NULL)
		return -1;
	if (ibuf_get(ibuf, rv, n) == -1) {
		free(rv);
		return -1;
	}
	rv[n] = '\0';

	*s = rv;

	return 0;
}
