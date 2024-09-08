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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header.h"

static char *strip_trailing(char *);

int
header_read(FILE *fp, struct getline *gl, struct header *out, int tv)
{
	size_t vlen;
	ssize_t len;

	if ((len = getline(&gl->line, &gl->n, fp)) == -1) {
		if (ferror(fp))
			return HEADER_ERR;
		return HEADER_EOF;
	}
	if (gl->line[len - 1] == '\n')
		gl->line[len - 1] = '\0';
	if (*gl->line == '\0')
		return HEADER_EOF;

	if ((out->val = strchr(gl->line, ':')) == NULL)
		return HEADER_ERR;
	*out->val++ = '\0';
	/* strip leading ws */
	out->val += strspn(out->val, " \t");
	if (tv)
		strip_trailing(out->val);

	out->key = gl->line;
	strip_trailing(out->key);

	for (size_t i = 0; out->key[i] != '\0'; i++) {
		if (!isprint( (unsigned char) out->key[i]))
			return HEADER_ERR;
	}

	for (vlen = 0; out->val[vlen] != '\0'; vlen++) {
		if (!isascii( (unsigned char) out->val[vlen]))
			return HEADER_ERR;
	}

	if ((out->key = strdup(out->key)) == NULL)
		return HEADER_ERR;
	if ((out->val = strndup(out->val, vlen)) == NULL)
		goto key;

	for (;;) {
		char *line;
		int c;
		void *t;
		char *e;

		if ((c = fgetc(fp)) == EOF) {
			if (ferror(fp))
				goto val;
			break;
		}
		if (!isspace(c) || c == '\n') {
			if (fseek(fp, -1, SEEK_CUR) == -1)
				goto val;
			break;
		}

		if ((len = getline(&gl->line, &gl->n, fp)) == -1)
			goto val;
		if (tv && gl->line[len - 1] == '\n')
			gl->line[len - 1] = '\0';

		if (tv) {
			line = gl->line + strspn(gl->line, " \t");
			e = strip_trailing(line);
			len = e - line;
		}
		else
			line = gl->line;
		/* len is fine as is */

		for (size_t i = 0; line[i] != '\0'; i++) {
			if (!isascii( (unsigned char) line[i]))
				goto val;
		}

		t = realloc(out->val, vlen + len + 2);
		if (t == NULL)
			goto val;
		out->val = t;
		out->val[vlen] = c;
		memcpy(&out->val[vlen + 1], line, len);
		out->val[vlen + 1 + len] = '\0';
		vlen += len + 1;
	}

	return 0;

	val:
	free(out->val);
	key:
	free(out->key);
	return HEADER_ERR;
}

static char *
strip_trailing(char *p)
{
	char *e;

	e = &p[strlen(p) - 1];
	while (e > p && (*e == ' ' || *e == '\t'))
		e--;
	if (e != p)
		e++;
	*e = '\0';
	return e;
}
