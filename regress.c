#define MAILBOX_INTERNALS

#include <sys/wait.h>

#include <dirent.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mail.h"
#include "mailbox.h"

struct test {
	const char *id;
	int (*fn) (void);
};

static int letter_test(void);

static int test_cmp(const void *, const void *);

struct test tests[] =
{
	{ "letter", letter_test },
};

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

int
main(int argc, char *argv[])
{
	int errors, failures, successes;

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

	errors = failures = successes = 0;
	for (size_t i = 0; i < nitems(tests); i++) {
		pid_t pid;
		int status;

		switch (pid = fork()) {
		case -1:
			errors++;
			continue;
		case 0:
			execl("./regress", "./regress", tests[i].id, NULL);
			err(1, "execl");
		default:
			if (waitpid(pid, &status, 0) == -1 || (status != 0 && status != 2)) {
				errors++;
				fprintf(stderr, "test %s errored\n", tests[i].id);
			}
			else if (status == 2) {
				failures++;
				fprintf(stderr, "test %s failed\n", tests[i].id);
			}
			else
				successes++;
			break;
		}
	}

	fprintf(stderr, "%d tests succeeded, %d tests failed, %d tests errored\n",
		successes, failures, errors);
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

static int
letter_test(void)
{
	FILE *fp;
	struct letter letter;

	if (pledge("stdio rpath", NULL) == -1)
		err(1, "pledge");
	if ((fp = fopen("tests/letter", "r")) == NULL)
		err(1, "fopen");
	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (read_letter(fp, &letter) == -1) {
		fclose(fp);
		return 1;
	}

	if (strcmp(letter.from, "gary@nota.realdomain") != 0)
		return 1;
	if (letter.subject == NULL || strcmp(letter.subject, "Test mail") != 0)
		return 1;
	if (letter.date != 1718936773)
		return 1;

	free(letter.subject);
	free(letter.from);
	fclose(fp);

	return 0;
}
