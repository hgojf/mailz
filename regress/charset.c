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
#include <locale.h>
#include <stdio.h>
#include <string.h>

#include "../charset.h"
#include "../encoding.h"
#include "charset.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

void
charset_getc_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *out;
		enum charset_type charset;
		int error;
	} tests[] = {
		{ "hi", "hi", CHARSET_ASCII, 0 },
		{ "hi\xFF", "hi", CHARSET_ASCII, -1 },

		{ "hi", "hi", CHARSET_ISO_8859_1, 0 },
		{ "hi\xFF", "hi\xC3\xBF", CHARSET_ISO_8859_1, 0 },

		{ "hi", "hi", CHARSET_UTF8, 0 },
		{ "hi\xC3\xBF", "hi\xC3\xBF", CHARSET_UTF8, 0 },
		{ "hi\xFF", "hi", CHARSET_UTF8, -1 },

		{ "hi", "hi", CHARSET_OTHER, 0 },
		{ "hi\xFF", "hi\xEF\xBF\xBD", CHARSET_OTHER, 0 },
	};

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		errx(1, "setlocale");

	for (i = 0; i < nitems(tests); i++) {
		struct charset charset;
		struct encoding decoder;
		FILE *fp;
		size_t outi, outsz;
		char buf[4];
		int n;

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		encoding_from_type(&decoder, ENCODING_BINARY);
		charset_from_type(&charset, tests[i].charset);
		outi = 0;
		outsz = strlen(tests[i].out);

		while ((n = charset_getc(&charset, &decoder, fp, buf)) != tests[i].error) {
			if (n == 0)
				errx(1, "early end-of-file %zu", i);
			if (n == -1)
				errx(1, "invalid input");
			if (outi + n > outsz)
				errx(1, "output was too long");
			if (memcmp(&tests[i].out[outi], buf, n) != 0)
				errx(1, "output was incorrect");
			outi += n;
		}
		if (outi != outsz)
			errx(1, "early end-of-file");

		fclose(fp);
	}
}
