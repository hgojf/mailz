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

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "date.h"

static const char *months[12] = {	"Jan", "Feb", "Mar", "Apr", "May", 
									"Jun", "Jul", "Aug", "Sep", "Oct",
									"Nov", "Dec" };
static const char *weekdays[7] = {	"Sun", "Mon", "Tue", "Wed", "Thu", 
									"Fri", "Sat" };

#define SEC_TO_HR(s) ((s) / (60 * 60))
#define SEC_TO_MIN(s) ((s) / 60)

/*
 * Formats a date in RFC 5322 format.
 * The fields of 'tm' should be relative to
 * the timezone offset given by 'off', not UTC.
 */
int
date_format(struct tm* tm, long off, char buf[static EMAIL_DATE_LEN])
{
	long hr, mn, absoff;
	int n;

	if ((absoff = off) < 0)
		absoff = -off;
	hr = SEC_TO_HR(absoff);
	mn = SEC_TO_MIN(absoff) - (hr * 60);

	if (tm->tm_wday < 0 || tm->tm_wday > 6)
		return 0;
	if (tm->tm_wday < 0 || tm->tm_wday > 6)
		return -1;
	if (tm->tm_mon < 0 || tm->tm_mon > 11)
		return -1;

	n = snprintf(buf, EMAIL_DATE_LEN, "%s, %02d %s %02d %02d:%02d:%02d %c%02ld%02ld", 
		weekdays[tm->tm_wday], tm->tm_mday, months[tm->tm_mon],	
		tm->tm_year + 1900, tm->tm_hour, tm->tm_min, tm->tm_sec, 
		off < 0 ? '-' : '+', hr, mn);

	if (n < 0 || n >= EMAIL_DATE_LEN)
		return -1;
	return 0;
}

time_t
date_parse(const char *s)
{
	struct tm tm;
	time_t date;
	const char *e, *fmt;
	long off;

	if (strchr(s, ',') != NULL)
		fmt = "%a, %d %b %Y %H:%M:%S %z";
	else
		fmt = "%d %b %Y %H:%M:%S %z";

	memset(&tm, 0, sizeof(tm));

	/* XXX: do this stuff manually */
	if ((e = strptime(s, fmt, &tm)) == NULL)
		return -1;

	/* check for any gunk after the date except comments */
	if (*e != '\0' && e[strspn(e, " \t")] != '(')
		return -1;

	off = tm.tm_gmtoff;
	if ((date = timegm(&tm)) == -1)
		return -1;

	date -= off;

	return date;
}
