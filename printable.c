#include <ctype.h>
#include <string.h>
#include <wchar.h>

#include "printable.h"

int
string_isprint(const char *s)
{
	mbstate_t mbs;
	size_t len, n;

	memset(&mbs, 0, sizeof(mbs));

	len = strlen(s);
	while (len != 0) {
		switch (n = mbrtowc(NULL, s, len, &mbs)) {
		case -1:
		case -3:
			return 0;
		case 0: /* NUL */
			return 0;
		case -2:
			len -= 1;
			s += 1;
			break;
		default:
			if (n == 1) {
				if (!isprint((unsigned char)*s) && !isspace((unsigned char)*s))
					return 0;
			}

			len -= n;
			s += n;
			break;
		}
	}

	return 1;
}

