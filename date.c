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
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "date.h"

struct timezone_offset {
	int negative;
	int minute;
	int hour;
};

/* rfc 5322 date parsing */

static int tzparse(const char *, struct timezone_offset *);

time_t
tz_tosec(const char *s)
{
	time_t rv;
	struct timezone_offset off;

	if (tzparse(s, &off) == -1)
		return TZ_INVALIDSEC;

	rv = (off.hour * 60 * 60) + (off.minute * 60);
	if (off.negative)
		rv = -rv;
	return rv;
}

static int
tzparse(const char *tz, struct timezone_offset *out)
{
	int hr, mn, ng;
	size_t len;

	len = strlen(tz);

	/* defaults */
	mn = 0;
	ng = 0;
	switch (len) {
	case 1: {
		/* Military time zone codes, a-i, j-z, all 0 */
		char u;
		u = toupper( (unsigned char) tz[0]);
		if (u < 'A' || u == 'J' || u > 'Z')
			return -1;
		hr = 0;
		ng = 1;
		break;
	}
	case 2:
		/* GMT alias */
		if (memcmp(tz, "UT", 2) == 0) {
			hr = 0;
		}
		else
			return -1;
		break;
	case 3:
		/* USA obsolete time zones + GMT */

		if (tz[2] != 'T')
			return -1;
		/* GMT */
		if (memcmp(tz, "GM", 2) == 0) {
			hr = 0;
			break;
		}
		switch (tz[0]) {
		case 'E':
			hr = -5;
			break;
		case 'C':
			hr = -6;
			break;
		case 'M':
			hr = -7;
			break;
		case 'P':
			hr = -8;
			break;
		default:
			return -1;
		}
		if (tz[1] == 'D')
			hr += 1;
		else if (tz[1] != 'S')
			return -1;
		break;
	case 5:
		/* official ([+-])DDDD utc offset */
		if (tz[0] != '+' && tz[0] != '-')
			return -1;
		ng = tz[0] == '-';
		for (int i = 1; i < 5; i++)
			if (!isdigit( (unsigned char) tz[i]))
				return -1;
		hr = 0;
		hr += (tz[1] - '0') * 10;
		hr += (tz[2] - '0');

		mn = 0;
		mn += (tz[3] - '0') * 10;
		mn += (tz[4] - '0');

		if (hr > 23 || mn > 59)
			return -1;
		break;
	default:
		/* any other alphabetical time zone */
		for (size_t i = 0; i < len; i++)
			if (!isalpha( (unsigned char) tz[i]))
				return -1;
		hr = 0;
		ng = 1;
		break;
	}

	out->hour = hr;
	out->minute = mn;
	out->negative = ng;
	return 0;
}

int
date_test(void)
{
	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");
	if (tz_tosec("GMT") != 0)
		return 1;
	if (tz_tosec("-2300") != -82800)
		return 1;
	if (tz_tosec("+2300") != 82800)
		return 1;
	return 0;
}
