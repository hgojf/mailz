#include <ctype.h>
#include <stdlib.h>

#include "printable.h"

int
string_printable(const char *s, size_t sz)
{
	size_t i;

	for (i = 0; i < sz; i++) {
		int ch;

		ch = (unsigned char)s[i];
		if (ch == '\0')
			return 1;
		if (!isprint(ch) && !isspace(ch))
			return 0;
	}

	/* no NUL terminator */
	return 0;
}
