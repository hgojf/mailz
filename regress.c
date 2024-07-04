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

#define REGRESS

#include <sys/wait.h>

#include <dirent.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "address.h"
#include "config.h"
#include "date.h"
#include "mail.h"
#include "mailbox.h"

struct test {
	const char *id;
	int (*fn) (void);
};

static int test_cmp(const void *, const void *);

struct test tests[] =
{
	{ "date", date_test },
	{ "from", from_test },
	{ "letter", letter_test },
};

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

int
main(int argc, char *argv[])
{
	int failures, successes;

	if (strcmp(argv[0], "./regress") != 0)
		return 1;

	if (argc == 2) {
		const struct test *test;

		test = bsearch(argv[1], tests, nitems(tests), sizeof(*tests), test_cmp);
		if (test == NULL)
			errx(1, "unknown test %s", argv[1]);
		return test->fn() == 0 ? 0 : 2;
	}
	else if (argc != 1)
		errx(1, "invalid usage");

	failures = successes = 0;
	for (size_t i = 0; i < nitems(tests); i++) {
		pid_t pid;
		int status;

		switch (pid = fork()) {
		case -1:
			err(1, "fork");
			continue;
		case 0:
			execl("./regress", "./regress", tests[i].id, NULL);
			err(1, "execl");
		default:
			if (waitpid(pid, &status, 0) == -1 || (status != 0)) {
				failures++;
				fprintf(stderr, "test %s failed\n", tests[i].id);
			}
			else
				successes++;
			break;
		}
	}

	fprintf(stderr, "%d tests succeeded, %d tests failed\n",
		successes, failures);
}

static int
test_cmp(const void *one, const void *two)
{
	const char *n1;
	const struct test *n2;

	n1 = one;
	n2 = two;

	return strcmp(n1, n2->id);
}
