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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "header.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

static long header_date_timezone(const char *);
static long header_date_timezone_std(const char *, size_t);
static long header_date_timezone_usa(const char *, size_t);
static int header_token(FILE *, struct header_lex *, char *, size_t, int *);
static size_t strip_trailing(const char *, size_t);

static const char *days[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};
static const char *months[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

int
header_address(FILE *fp, struct header_address *from, int *eof)
{
	struct header_lex lex;
	size_t n;
	int state;

	if (*eof)
		return HEADER_EOF;

	if (from->addrsz == 0)
		return HEADER_INVALID;

	lex.cstate = 0;
	lex.echo = NULL;
	lex.qstate = 0;
	lex.skipws = 1;

	n = 0;
	state = 0;
	for (;;) {
		int ch;

		if ((ch = header_lex(fp, &lex)) < 0 && ch != HEADER_EOF)
			return ch;

		if (state == 0) {
			if (ch == HEADER_EOF || ch == ',') {
				n = strip_trailing(from->addr, n);
				from->addr[n] = '\0';
				if (from->namesz != 0)
					from->name[0] = '\0';

				if (ch == HEADER_EOF) {
					*eof = 1;
					if (n == 0)
						return HEADER_EOF;
				}

				return HEADER_OK;
			}

			if (ch == '<') {
				if (from->namesz != 0) {
					n = strip_trailing(from->addr, n);
					if (n >= from->namesz)
						return HEADER_INVALID;
					memcpy(from->name, from->addr, n);
					from->name[n] = '\0';
				}
				n = 0;
				state = 1;
				continue;
			}

			if (n == from->addrsz - 1)
				return HEADER_INVALID;
			from->addr[n++] = ch;
		}

		if (state == 1) {
			if (ch == HEADER_EOF)
				return HEADER_INVALID;
			if (ch == '>') {
				state = 2;
				continue;
			}

			if (n == from->addrsz - 1)
				return HEADER_INVALID;
			from->addr[n++] = ch;
		}

		if (state == 2) {
			if (ch == HEADER_EOF || ch == ',') {
				from->addr[n] = '\0';

				if (ch == HEADER_EOF)
					*eof = 1;
				return HEADER_OK;
			}
		}
	}
}

int
header_copy(FILE *in, FILE *out)
{
	struct header_lex lex;
	int ch;

	lex.cstate = -1;
	lex.echo = NULL;
	lex.qstate = -1;
	lex.skipws = 0;

	while ((ch = header_lex(in, &lex)) != HEADER_EOF) {
		if (ch < 0)
			return ch;
		if (fputc(ch, out) == EOF)
			return HEADER_OUTPUT;
	}

	return HEADER_OK;
}

int
header_copy_addresses(FILE *in, FILE *out, const char *exclude, int *any)
{
	char addr[255], name[65];
	struct header_address from;
	int eof, n;

	from.addr = addr;
	from.addrsz = sizeof(addr);

	from.name = name;
	from.namesz = sizeof(name);

	eof = 0;
	while ((n = header_address(in, &from, &eof)) != HEADER_EOF) {
		if (n < 0)
			return n;
		if (!strcmp(addr, exclude))
			continue;

		if (*any)
			if (fprintf(out, ",") < 0)
				return HEADER_OUTPUT;

		if (strlen(name) != 0) {
			if (fprintf(out, " %s <%s>", name, addr) < 0)
				return HEADER_OUTPUT;
		}
		else {
			if (fprintf(out, " %s", addr) < 0)
				return HEADER_OUTPUT;
		}
		*any = 1;
	}

	return HEADER_OK;
}


int
header_date(FILE *fp, time_t *dp)
{
	struct header_lex lex;
	struct tm tm;
	char buf[100], *bufp, *e, *s;
	const char *errstr;
	size_t i;
	time_t date;
	long off;
	int eof;

	lex.cstate = 0;
	lex.echo = NULL;
	lex.qstate = 0;
	lex.skipws = 1;

	memset(&tm, 0, sizeof(tm));

	eof = 0;
	if (header_token(fp, &lex, buf, sizeof(buf), &eof) != HEADER_OK)
		return HEADER_INVALID;

	if ((e = strchr(buf, ',')) != NULL) {
		if (e[1] != '\0')
			return HEADER_INVALID;
		*e = '\0';
		tm.tm_wday = -1;
		for (i = 0; i < nitems(days); i++) {
			if (!strcmp(buf, days[i])) {
				tm.tm_wday = i;
				break;
			}
		}
		if (tm.tm_wday == -1)
			return HEADER_INVALID;

		if (header_token(fp, &lex, buf, sizeof(buf), &eof) != HEADER_OK)
			return HEADER_INVALID;
	}

	tm.tm_mday = strtonum(buf, 1, 31, &errstr);
	if (errstr != NULL)
		return HEADER_INVALID;

	if (header_token(fp, &lex, buf, sizeof(buf), &eof) != HEADER_OK)
		return HEADER_INVALID;

	tm.tm_mon = -1;
	for (i = 0; i < nitems(months); i++) {
		if (!strcmp(buf, months[i])) {
			tm.tm_mon = i;
			break;
		}
	}
	if (tm.tm_mon == -1)
		return HEADER_INVALID;

	if (header_token(fp, &lex, buf, sizeof(buf), &eof) != HEADER_OK)
		return HEADER_INVALID;

	tm.tm_year = strtonum(buf, 0, 9999, &errstr);
	if (errstr != NULL)
		return HEADER_INVALID;
	if (tm.tm_year <= 49)
		tm.tm_year += 2000;
	else if (tm.tm_year <= 999)
		tm.tm_year += 1900;
	tm.tm_year -= 1900;

	if (header_token(fp, &lex, buf, sizeof(buf), &eof) != HEADER_OK)
		return HEADER_INVALID;

	bufp = buf;

	if ((s = strsep(&bufp, ":")) == NULL)
		return HEADER_INVALID;
	tm.tm_hour = strtonum(s, 0, 23, &errstr);
	if (errstr != NULL)
		return HEADER_INVALID;

	if ((s = strsep(&bufp, ":")) == NULL)
		return HEADER_INVALID;
	tm.tm_min = strtonum(s, 0, 59, &errstr);
	if (errstr != NULL)
		return HEADER_INVALID;

	if ((s = bufp) != NULL) {
		tm.tm_sec = strtonum(s, 0, 60, &errstr);
		if (errstr != NULL)
			return HEADER_INVALID;
	}

	if (header_token(fp, &lex, buf, sizeof(buf), &eof) != HEADER_OK)
		return HEADER_INVALID;
	if ((off = header_date_timezone(buf)) == -1)
		return HEADER_INVALID;

	if (header_token(fp, &lex, buf, sizeof(buf), &eof) != HEADER_EOF)
		return HEADER_INVALID;

	if ((date = timegm(&tm)) == -1)
		return HEADER_INVALID;

	*dp = date - off;
	return HEADER_OK;
}

static long
header_date_timezone(const char *s)
{
	size_t len;
	long rv;

	len = strlen(s);

	if ((rv = header_date_timezone_std(s, len)) != -1)
		return rv;
	if ((rv = header_date_timezone_usa(s, len)) != -1)
		return rv;

	if (!strcmp(s, "UT") || !strcmp(s, "GMT"))
		return 0;

	return -1;
}

static long
header_date_timezone_std(const char *s, size_t len)
{
	const char *errstr;
	char nbuf[3];
	long rv;

	if (len != 5)
		return -1;

	memcpy(nbuf, &s[1], 2);
	nbuf[2] = '\0';

	rv = strtonum(nbuf, 0, 99, &errstr) * 60 * 60;
	if (errstr != NULL)
		return -1;

	memcpy(nbuf, &s[3], 2);
	nbuf[2] = '\0';

	rv += strtonum(nbuf, 0, 59, &errstr) * 60;
	if (errstr != NULL)
		return -1;

	if (s[0] == '-')
		rv = -rv;
	else if (s[0] != '+')
		return -1;

	return rv;
}

static long
header_date_timezone_usa(const char *s, size_t len)
{
	long hr;

	if (len != 3)
		return -1;

	switch (s[0]) {
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

	if (s[1] == 'D')
		hr -= 1;
	else if (s[1] != 'S')
		return -1;

	if (s[2] != 'T')
		return -1;

	return hr * 60 * 60;
}

int
header_encoding(FILE *fp, FILE *echo, char *buf, size_t bufsz)
{
	struct header_lex lex;
	size_t n;
	int ch;

	lex.cstate = 0;
	lex.echo = echo;
	lex.qstate = 0;
	lex.skipws = 1;

	n = 0;
	while ((ch = header_lex(fp, &lex)) != HEADER_EOF) {
		if (ch < 0)
			return ch;
		if (n != bufsz)
			buf[n++] = ch;
	}

	if (n != bufsz)
		buf[n] = '\0';
	return n == bufsz ? HEADER_TRUNC : HEADER_OK;
}

int
header_lex(FILE *fp, struct header_lex *lex)
{
	for (;;) {
		int ch;

		if ((ch = fgetc(fp)) == EOF)
			goto eof;
		if (ch == '\n') {
			if ((ch = fgetc(fp)) == EOF)
				goto eof;
			if (ch != ' ' && ch != '\t') {
				if (ungetc(ch, fp) == EOF)
					return HEADER_EOF;
				goto eof;
			}
		}

		if (lex->echo != NULL) {
			if (fputc(ch, lex->echo) == EOF)
				return HEADER_OUTPUT;
		}

		if (lex->cstate != -1) {
			if (ch == '(') {
				if (lex->cstate == INT_MAX)
					return HEADER_INVALID;
				lex->cstate++;
				continue;
			}

			if (lex->cstate > 0) {
				if (ch == ')')
					lex->cstate--;
				continue;
			}
		}

		if (lex->qstate != -1) {
			if (ch == '\"') {
				lex->qstate = !lex->qstate;
				continue;
			}
		}

		if (lex->skipws) {
			if (ch == ' ' || ch == '\t')
				continue;
			lex->skipws = 0;
		}

		return ch;
	}

	eof:
	if (lex->echo != NULL) {
		if (fputc('\n', lex->echo) == EOF)
			return HEADER_OUTPUT;
	}

	if (lex->cstate != -1 && lex->cstate != 0)
		return HEADER_INVALID;
	if (lex->qstate != -1 && lex->qstate != 0)
		return HEADER_INVALID;
	return HEADER_EOF;
}


int
header_message_id(FILE *fp, char *buf, size_t bufsz)
{
	struct header_lex lex;
	size_t n;
	int state;

	if (bufsz == 0)
		return HEADER_INVALID;

	lex.cstate = 0;
	lex.echo = NULL;
	lex.qstate = 0;
	lex.skipws = 0;

	n = 0;
	state = 0;
	for (;;) {
		int ch;

		if ((ch = header_lex(fp, &lex)) < 0 && ch != HEADER_EOF)
			return ch;

		if (state == 2) {
			if (ch == HEADER_EOF)
				break;
			continue;
		}

		if (ch == HEADER_EOF)
			return HEADER_INVALID;

		if (state == 0) {
			if (ch == '<')
				state = 1;
			continue;
		}

		if (ch == '>') {
			state = 2;
			continue;
		}

		if (!isprint(ch) && !isspace(ch))
			return HEADER_INVALID;
		if (n == bufsz - 1)
			return HEADER_INVALID;
		buf[n++] = ch;
	}

	buf[n] = '\0';
	return HEADER_OK;
}

int
header_name(FILE *fp, char *buf, size_t bufsz)
{
	size_t n;

	if (bufsz == 0)
		return HEADER_INVALID;

	n = 0;
	for (;;) {
		int ch;

		if ((ch = fgetc(fp)) == EOF)
			return HEADER_INVALID;
		if (ch == ':')
			break;
		if (ch == '\n' && n == 0)
			return HEADER_EOF;

		if (ch < 33 || ch > 126)
			return HEADER_INVALID;

		if (n == bufsz - 1)
			return HEADER_INVALID;
		buf[n++] = ch;
	}

	buf[n] = '\0';
	return HEADER_OK;
}

int
header_skip(FILE *in, FILE *echo)
{
	struct header_lex lex;
	int ch;

	lex.cstate = -1;
	lex.echo = echo;
	lex.qstate = -1;
	lex.skipws = 0;

	while ((ch = header_lex(in, &lex)) != HEADER_EOF) {
		if (ch < 0)
			return ch;
	}
	return HEADER_OK;
}

int
header_subject(FILE *fp, char *buf, size_t bufsz)
{
	struct header_lex lex;
	size_t n;
	int ch;

	if (bufsz == 0)
		return HEADER_INVALID;

	lex.cstate = -1;
	lex.echo = NULL;
	lex.qstate = -1;
	lex.skipws = 1;

	n = 0;
	while ((ch = header_lex(fp, &lex)) != HEADER_EOF) {
		if (ch < 0)
			return ch;
		if (!isspace(ch) && !isprint(ch))
			continue; /* ignore */

		if (n == bufsz - 1)
			continue; /* truncate */
		buf[n++] = ch;
	}

	buf[n] = '\0';
	return HEADER_OK;
}

int
header_subject_reply(FILE *in, FILE *out)
{
	struct header_lex lex;
	const char *re;
	size_t i;
	int ch;

	lex.cstate = -1;
	lex.echo = NULL;
	lex.qstate = -1;
	lex.skipws = 1;

	if (fprintf(out, "Subject: Re: ") < 0)
		return HEADER_OUTPUT;

	re = "Re: ";
	i = 0;
	while ((ch = header_lex(in, &lex)) != HEADER_EOF) {
		if (ch < 0)
			return ch;

		if (re[i] != '\0') {
			if (re[i] == ch) {
				i++;
				continue;
			}

			if (i != 0) {
				if (fwrite(re, i, 1, out) != 1)
					return HEADER_OUTPUT;
			}
			re = "";
			i = 0;
		}

		if (fputc(ch, out) == EOF)
			return HEADER_OUTPUT;
	}

	if (fprintf(out, "\n") < 0)
		return HEADER_OUTPUT;

	return HEADER_OK;
}

static int
header_token(FILE *fp, struct header_lex *lex, char *buf,
	     size_t bufsz, int *eof)
{
	size_t n;

	if (*eof)
		return HEADER_EOF;

	if (bufsz == 0)
		return HEADER_INVALID;
	lex->skipws = 1;

	n = 0;
	for (;;) {
		int ch;

		if ((ch = header_lex(fp, lex)) < 0 && ch != HEADER_EOF)
			return ch;
		if (ch == HEADER_EOF) {
			*eof = 1;
			if (n == 0)
				return HEADER_EOF;
			break;
		}

		if (ch == ' ' || ch == '\t')
			break;

		if (n == bufsz -1)
			return HEADER_INVALID;
		buf[n++] = ch;
	}

	buf[n] = '\0';
	return HEADER_OK;
}

size_t
strip_trailing(const char *s, size_t n)
{
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
		n--;
	return n;
}
