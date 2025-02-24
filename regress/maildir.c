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
#include <stdlib.h>
#include <string.h>

#include "../maildir.h"
#include "maildir.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

#define MAILDIR_ERROR 0
#define MAILDIR_OK 1
#define MAILDIR_SAME 2

void
maildir_get_flag_test(void)
{
	size_t i;
	const struct {
		const char *in;
		int flag;
		int error;
	} tests[] = {
		{ "hi", 'S', 0 },
		{ "hi:2,", 'S', 0 },
		{ "hi:3,S", 'S', 0 },
		{ "hi:", 'S', 0 },

		{ "hi:2,S", 'S', 1 },
	};

	for (i = 0; i < nitems(tests); i++) {
		int error;

		error = maildir_get_flag(tests[i].in, tests[i].flag);
		if (error != tests[i].error)
			errx(1, "wrong error");
	}
}

void
maildir_unset_flag_test(void)
{
	size_t i;
	const struct {
		const char *in;
		const char *out;
		size_t bufsz;
		int flag;
		int error;
	} tests[] = {
		{ "hi:2,S", "hi:2,", 255, 'S', MAILDIR_OK },
		{ "hi:2,ASU", "hi:2,AU", 255, 'S', MAILDIR_OK },

		{ "hi:2,S", "hi:2,", 6, 'S', MAILDIR_OK },
		{ "hi:2,S", "hi:2,", 5, 'S', MAILDIR_ERROR },

		{ "hi:2,", NULL, 255, 'S', MAILDIR_SAME },
		{ "hi", NULL, 255, 'S', MAILDIR_SAME },
	};

	for (i = 0; i < nitems(tests); i++) {
		char buf[255];
		const char *error;

		error = maildir_unset_flag(tests[i].in, tests[i].flag,
					   buf, tests[i].bufsz);
		switch (tests[i].error) {
		case MAILDIR_ERROR:
			if (error != NULL)
				errx(1, "wrong error");
			break;
		case MAILDIR_OK:
			if (error != buf || strcmp(buf, tests[i].out) != 0)
				errx(1, "wrong error");
			break;
		case MAILDIR_SAME:
			if (error != tests[i].in)
				errx(1, "wrong error");
			break;
		}
	}
}
