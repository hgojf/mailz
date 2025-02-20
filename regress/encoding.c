#include <err.h>
#include <stdio.h>

#include "../encoding.h"
#include "encoding.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

void
encoding_getc_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *out;
		size_t insz;
		size_t outsz;
		enum encoding_type encoding;
		int error;
	} tests[] = {
		/*
		 * This macro is needed for input/output with NUL bytes.
		 */
		#define test(in, out, encoding, error) \
			{ in, out, sizeof(in) - 1, sizeof(out) - 1, encoding, error }
		test("hi", "hi", ENCODING_7BIT, ENCODING_EOF),
		test("hi\xFF", "hi", ENCODING_7BIT, ENCODING_ERR),
		test("hi\0", "hi", ENCODING_7BIT, ENCODING_ERR),

		test("hi", "hi", ENCODING_8BIT, ENCODING_EOF),
		test("hi\xFF", "hi\xFF", ENCODING_8BIT, ENCODING_EOF),
		test("hi\0", "hi", ENCODING_8BIT, ENCODING_ERR),

		test("aGk=", "hi", ENCODING_BASE64, ENCODING_EOF),
		test("aG\nk=", "hi", ENCODING_BASE64, ENCODING_EOF),
		test("===", "", ENCODING_BASE64, ENCODING_ERR),
		test("\xFF", "", ENCODING_BASE64, ENCODING_ERR),
		test("\0", "", ENCODING_BASE64, ENCODING_ERR),

		test("hi", "hi", ENCODING_BINARY, ENCODING_EOF),
		test("hi\xFF", "hi\xFF", ENCODING_BINARY, ENCODING_EOF),
		test("hi\0", "hi\0", ENCODING_BINARY, ENCODING_EOF),

		test("hi", "hi", ENCODING_QP, ENCODING_EOF),
		test("h=\ni", "hi", ENCODING_QP, ENCODING_EOF),
		test("hi=FF", "hi\xFF", ENCODING_QP, ENCODING_EOF),
		test("hi\xFF", "hi", ENCODING_QP, ENCODING_ERR),
		#undef test
	};

	for (i = 0; i < nitems(tests); i++) {
		struct encoding decoder;
		FILE *fp;
		size_t outi;
		int ch;

		fp = fmemopen(tests[i].in, tests[i].insz, "r");
		if (fp == NULL)
			err(1, "fmemopen");

		encoding_from_type(&decoder, tests[i].encoding);
		outi = 0;
		while ((ch = encoding_getc(&decoder, fp)) != tests[i].error) {
			if (ch == ENCODING_EOF)
				errx(1, "early end-of-file");
			if (ch == ENCODING_ERR)
				errx(1, "invalid input");
			if (outi == tests[i].outsz)
				errx(1, "too much output");
			if ((unsigned char)tests[i].out[outi++] != ch)
				errx(1, "incorrect output");
		}
		if (outi != tests[i].outsz)
			errx(1, "early end-of-file");

		fclose(fp);
	}
}
