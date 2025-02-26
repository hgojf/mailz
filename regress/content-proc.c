#include <err.h>
#include <fcntl.h>
#include <locale.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "content-proc.h"
#include "../content-proc.h"
#include "../pathnames.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

void
content_proc_letter_test(void)
{
	size_t i;
	int null;
	const struct {
		const char *in;
	} tests[] = {
		{ "1" },
		{ "2" },
	};

	if ((null = open(PATH_DEV_NULL, O_RDONLY | O_CLOEXEC)) == -1)
		err(1, "%s", PATH_DEV_NULL);
	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		err(1, "setlocale");

	for (i = 0; i < nitems(tests); i++) {
		struct content_letter lr;
		struct content_proc pr;
		char path[PATH_MAX];
		FILE *out;
		int in, n;

		if (content_proc_init(&pr, "./mailz-content", null) == -1)
			errx(1, "content_proc_init");

		n = snprintf(path, sizeof(path),
			     "regress/letters/letter_in_%s",
			     tests[i].in);
		if (n < 0)
			err(1, "snprintf");
		if ((size_t)n >= sizeof(path))
			errx(1, "snprintf overflow");
		if ((in = open(path, O_RDONLY | O_CLOEXEC)) == -1)
			err(1, "%s", path);

		if (content_letter_init(&pr, &lr, in) == -1)
			errx(1, "content_letter_init");

		n = snprintf(path, sizeof(path),
			     "regress/letters/letter_out_%s",
			     tests[i].in);
		if (n < 0)
			err(1, "snprintf");
		if ((size_t)n >= sizeof(path))
			errx(1, "snprintf overflow");
		if ((out = fopen(path, "r")) == NULL)
			err(1, "%s", path);

		for (;;) {
			char buf[4], buf2[4];

			n = content_letter_getc(&lr, buf);
			if (n == -1)
				errx(1, "content_letter_getc");
			if (n == 0) {
				if (fgetc(out) != EOF)
					errx(1, "wrong output");
				break;
			}

			if (fread(buf2, n, 1, out) != 1)
				errx(1, "wrong output 1");
			if (memcmp(buf, buf2, n) != 0)
				errx(1, "wrong output");
		}

		content_letter_close(&lr);
		content_proc_kill(&pr);
		fclose(out);
	}
}

void
content_proc_summary_test(void)
{
	size_t i;
	int null;
	const struct {
		const char *in;
		const char *from;
		const char *subject;
		time_t date;
		int error;
	} tests[] = {
		{ "1", "dave@bogus.invalid", "Hello", 0, 0 },
		{ "2", "dave@bogus.invalid", NULL, 0, 0 },
	};

	if ((null = open(PATH_DEV_NULL, O_RDONLY | O_CLOEXEC)) == -1)
		err(1, "%s", PATH_DEV_NULL);

	for (i = 0; i < nitems(tests); i++) {
		struct content_proc pr;
		struct content_summary sm;
		char path[PATH_MAX];
		int error, fd, n;

		if (content_proc_init(&pr, "./mailz-content", null) == -1)
			errx(1, "content_proc_init");

		n = snprintf(path, sizeof(path), "regress/letters/summary_%s",
			     tests[i].in);
		if (n < 0)
			err(1, "snprintf");
		if ((size_t)n >= sizeof(path))
			errx(1, "snprintf overflow");

		fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd == -1)
			err(1, "%s", path);

		error = content_proc_summary(&pr, &sm, fd);
		if (error != tests[i].error)
			errx(1, "wrong error");

		if (error == 0) {
			if (sm.date != tests[i].date)
				errx(1, "wrong date");
			if (strcmp(sm.from, tests[i].from) != 0)
				errx(1, "wrong from address");
			if (sm.have_subject != (tests[i].subject != NULL))
				errx(1, "wrong subject");
			if (tests[i].subject != NULL
			    && strcmp(sm.subject, tests[i].subject) != 0)
				errx(1, "wrong subject");
		}

		content_proc_kill(&pr);
	}
}
