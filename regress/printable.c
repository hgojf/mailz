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
