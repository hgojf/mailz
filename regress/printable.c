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

#include "printable.h"
#include "../printable.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

void
string_printable_test(void)
{
	size_t i;
	const struct {
		const char *in;
		size_t sz;
		int printable;
	} tests[] = {
		#define test(in, printable) { in, sizeof(in), printable }
		test("hi", 1),
		test("hi\xFF", 0),
		{ "hi", 2, 0 },
		#undef test
	};

	for (i = 0; i < nitems(tests); i++) {
		int printable;

		printable = string_printable(tests[i].in, tests[i].sz);
		if (printable != tests[i].printable)
			errx(1, "wrong error");
	}
}
