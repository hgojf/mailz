#define MAILBOX_INTERNALS

#include <sys/wait.h>

#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "date.h"
#include "mail.h"
#include "mailbox.h"

struct test {
	const char *id;
	int (*fn) (void);
};

static int date_test(void);
static int from_test(void);
static int letter_test(void);
static int mbox_test(void);

static int test_cmp(const void *, const void *);

struct test tests[] =
{
	{ "date", date_test },
	{ "from", from_test },
	{ "letter", letter_test },
	{ "mbox", mbox_test },
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

static int
date_test(void)
{
	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");
	if (tz_tosec("GMT") != 0)
		return 1;
	if (tz_tosec("-2300") != -82800)
		return 1;
	if (tz_tosec("+2300") != 82800)
		return 1;
	return 0;
}

static int
from_test(void)
{
	struct from from;
	char *addr;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	addr = "Hello <user@invalid.gfy";
	if (from_extract(addr, &from) != -1)
		return 1;

	memset(&from, 0, sizeof(from));

	addr = "User <guy@valid.com>";
	if (from_extract(addr, &from) == -1)
		return 1;
	if (from.al != 13
		|| strncmp(from.addr, "guy@valid.com", 13) != 0
		|| from.nl != 4
		|| strncmp(from.name, "User", 4) != 0)
		return 1;

	memset(&from, 0, sizeof(from));

	addr = "guy@valid.com";
	if (from_extract(addr, &from) == -1)
		return 1;
	if (from.al != 13 
		|| strncmp(from.addr, "guy@valid.com", 13) != 0
		|| from.nl != 0)
		return 1;

	addr = "<odd@mail.com>";
	if (from_extract(addr, &from) == -1)
		return 1;
	if (from.nl != 0
		|| from.al != 12
		|| strncmp(from.addr, "odd@mail.com", 12) != 0)
		return 1;
	return 0;
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

	if (strcmp(letter.from, "A friend <gary@nota.realdomain>") != 0)
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

static int
mbox_test(void)
{
	struct mailbox mailbox;
	int fd;

	if (pledge("stdio rpath", NULL) == -1)
		err(1, "pledge");
	if ((fd = open("tests/mbox", O_RDONLY)) == -1)
		err(1, "open");
	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");
	if (mailbox_setup(fd, &mailbox) == -1)
		return 1;
	if (mailbox_read(&mailbox, 1) == -1)
		return 1;
	mailbox_free(&mailbox);
	return 0;
}
