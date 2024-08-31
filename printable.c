#include <ctype.h>
#include <string.h>
#include <wchar.h>

#include "printable.h"

/*
 * Determines if a string is safe to print.
 * A 'printable' string is considered to be one that is made up of
 * valid UTF-8, and does not contain ascii characters that are neither
 * printable nor whitespace characters.
 * This function relies on the thread locale LC_CTYPE category
 * being set to a UTF-8 locale.
 */
int
string_isprint(const char *s)
{
	mbstate_t mbs;
	size_t len;

	memset(&mbs, 0, sizeof(mbs));
	len = strlen(s);
	while (len != 0) {
		size_t n;

		switch ((n = mbrtowc(NULL, s, len, &mbs))) {
		case (size_t)-1:
		case (size_t)-3:
		case 0: /* NUL */
			return 0;
		case (size_t)-2:
			len -= n;
			s += n;
			break;
		default:
			if (n == 1 && !isprint((unsigned char)*s)
				&& !isspace((unsigned char)*s))
					return 0;
			len -= n;
			s += n;
			break;
		}
	}

	return 1;
}
