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
