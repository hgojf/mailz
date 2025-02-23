/*
 * Copyright (c) 2025 Henry Ford <fordhenry2299@gmail.com>

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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../header.h"
#include "header.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

void
header_lex_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *out;
		int raw;
		int error;
	} tests[] = {
		{ "hi", "hi", 0, HEADER_EOF },
		{ "hi\n there", "hi there", 0, HEADER_EOF },
		{ "hi(there)", "hi", 0, HEADER_EOF },
		{ "hi \"there\"", "hi there", 0, HEADER_EOF },
		{ "hi(\"", "hi(\"", 1, HEADER_EOF },
		{ "\n", "", 1, HEADER_EOF },

		{ "hi(", "hi", 0, HEADER_INVALID },
		{ "hi\"", "hi", 0, HEADER_INVALID },
	};

	for (i = 0; i < nitems(tests); i++) {
		struct header_lex lex;
		FILE *fp;
		const char *out;
		int error;

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		lex.echo = NULL;
		lex.cstate = tests[i].raw ? -1 : 0;
		lex.qstate = tests[i].raw ? -1 : 0;
		lex.skipws = tests[i].raw ? 0 : 1;

		out = tests[i].out;
		while ((error = header_lex(fp, &lex)) != tests[i].error) {
			if (error < 0)
				errx(1, "wrong error");
			if (*out == '\0' || *out++ != error)
				errx(1, "wrong output");
		}
		if (*out != '\0')
			errx(1, "wrong output");

		fclose(fp);
	}
}

void
header_name_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *out;
		size_t bufsz;
		int error;
	} tests[] = {
		{ "Cc:", "Cc", 10, HEADER_OK },

		{ "\n", NULL, 10, HEADER_EOF },

		{ "Cc", NULL, 10, HEADER_INVALID },
		{ "Cc\xFF:", NULL, 10, HEADER_INVALID },
		{ "Cc:", NULL, 2, HEADER_INVALID },
	};

	for (i = 0; i < nitems(tests); i++) {
		FILE *fp;
		char buf[10];
		int error;

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		error = header_name(fp, buf, tests[i].bufsz);
		if (error != tests[i].error)
			errx(1, "wrong error");
		if (error == HEADER_OK)
			if (strcmp(buf, tests[i].out) != 0)
				errx(1, "wrong output");

		fclose(fp);
	}
}

void
header_subject_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *out;
		size_t bufsz;
	} tests[] = {
		{ "hi", "hi", 10 },
		{ "h\xFFi", "hi", 10 },
		{ "hello", "hello", 6 },
		{ "hello", "hell", 5 },
	};

	for (i = 0; i < nitems(tests); i++) {
		FILE *fp;
		char buf[10];
		int error;

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		error = header_subject(fp, buf, tests[i].bufsz);
		if (error != HEADER_OK)
			errx(1, "wrong error");
		if (strcmp(buf, tests[i].out) != 0)
			errx(1, "wrong output");

		fclose(fp);
	}
}

void
header_subject_reply_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *out;
	} tests[] = {
		{ "hi", "hi" },
		{ "Re: hi", "hi" },
		{ "Resurrection", "Resurrection" },
	};

	for (i = 0; i < nitems(tests); i++) {
		FILE *in, *out;
		char *obuf;
		size_t osize;
		int error;

		in = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (in == NULL)
			err(1, "fmemopen");

		out = open_memstream(&obuf, &osize);
		if (out == NULL)
			err(1, "open_memstream");

		error = header_subject_reply(in, out);
		if (error != HEADER_OK)
			errx(1, "wrong error");

		fflush(out);
		#define reply "Subject: Re: "
		if (strncmp(obuf, reply, sizeof(reply) - 1) != 0)
			errx(1, "wrong output");
		if (obuf[osize - 1] != '\n')
			errx(1, "wrong output");
		obuf[osize - 1] = '\0';
		if (strcmp(&obuf[sizeof(reply) - 1], tests[i].out) != 0)
			errx(1, "wrong output");
		#undef reply

		fclose(in);
		fclose(out);
		free(obuf);
	}
}
