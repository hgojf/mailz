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

#include "header.h"

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
