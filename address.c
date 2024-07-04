#include <assert.h>
#include <err.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "address.h"

static int from_extract_internal(char *, struct from *);

int
from_safe_new(char *s, struct from_safe *out)
{
	struct from dumb;

	if (from_extract_internal(s, &dumb) == -1)
		return -1;
	out->str = s;
	return 0;
}

void
from_extract(const struct from_safe *s, struct from *out)
{
	/* 
	 * as long as this was created by from_safe_new 
	 * this should never happen
	 */
	if (from_extract_internal(s->str, out) == -1)
		assert(0);
}

static int
from_extract_internal(char *from, struct from *out)
{
	char *m;
	size_t al, nl;

	/* Real Name <addr>, <addr> */
	if ((m = strchr(from, '<')) != NULL) {
		al = strlen(m) - 2;
		if (al > INT_MAX)
			return -1;
		nl = (m - from);
		if (m != from) /* addresses in the form '<addr>' */
			nl -= 1;
		if (nl > INT_MAX)
			return -1;

		if (m[al + 1] != '>')
			return -1;

		out->addr = m + 1;
		out->al = al;
		out->name = from;
		out->nl = nl;
	}
	/* addr */
	else {
		al = strlen(from);
		if (al > INT_MAX)
			return -1;

		out->addr = from;
		out->al = al;
		out->nl = 0;
		out->name = NULL;
	}

	return 0;
}

int
from_test(void)
{
	struct from from;
	char *addr;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	addr = "Hello <user@invalid.gfy";
	if (from_extract_internal(addr, &from) != -1)
		return 1;

	memset(&from, 0, sizeof(from));

	addr = "User <guy@valid.com>";
	if (from_extract_internal(addr, &from) == -1)
		return 1;
	if (from.al != 13
		|| strncmp(from.addr, "guy@valid.com", 13) != 0
		|| from.nl != 4
		|| strncmp(from.name, "User", 4) != 0)
		return 1;

	memset(&from, 0, sizeof(from));

	addr = "guy@valid.com";
	if (from_extract_internal(addr, &from) == -1)
		return 1;
	if (from.al != 13 
		|| strncmp(from.addr, "guy@valid.com", 13) != 0
		|| from.nl != 0)
		return 1;

	addr = "<odd@mail.com>";
	if (from_extract_internal(addr, &from) == -1)
		return 1;
	if (from.nl != 0
		|| from.al != 12
		|| strncmp(from.addr, "odd@mail.com", 12) != 0)
		return 1;
	return 0;
}
