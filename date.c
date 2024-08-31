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

static int date_valid(struct tm *, long, long);

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

	if (!date_valid(tm, hr, mn))
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
date_parse(char *s)
{
	struct tm tm;
	time_t date;
	char *b;
	const char *fmt;
	long off;

	if ((b = strchr(s, '(')) != NULL)
		*b = '\0';

	if (strchr(s, ',') != NULL)
		fmt = "%a, %d %b %Y %H:%M:%S %z";
	else
		fmt = "%d %b %Y %H:%M:%S %z";

	memset(&tm, 0, sizeof(tm));

	/* XXX: do this stuff manually */
	if (strptime(s, fmt, &tm) == NULL)
		return -1;

	off = tm.tm_gmtoff;
	if ((date = timegm(&tm)) == -1)
		return -1;

	date -= off;

	return date;
}

/*
 * Checks if a given date and utc offset are valid per RFC 5322
 * section 3.3 paragraph 4
 */
static int
date_valid(struct tm *tm, long offh, long offm)
{
	if (tm->tm_wday < 0 || tm->tm_wday > 6)
		return 0;

	if (tm->tm_mon < 0 || tm->tm_mon > 11)
		return 0;

	if (tm->tm_mday < 1 || tm->tm_mday > 31)
		return 0;

	if (tm->tm_hour < 0 || tm->tm_hour > 23)
		return 0;

	if (tm->tm_min < 0 || tm->tm_min > 59)
		return 0;

	if (tm->tm_sec < 0 || tm->tm_sec > 60)
		return 0;

	/* 
	 * According to RFC 5322, the last two digits of the timezone
	 * offset (the minute offset) must be between 00 and 59. This 
	 * implies that the first two digits (the hour offset) must be
	 * no more than 2 digits. No other limits are imposed on the hour
	 * portion of the timezone offset.
	 */
	if (offh < -99 || offh > 99)
		return -1;

	if (offm < 0 || offm > 59)
		return 0;

	return 1;
}
