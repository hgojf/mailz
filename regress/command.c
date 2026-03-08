#include <err.h>
#include <stdio.h>
#include <string.h>

#include "command.h"
#include "../command.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

void
command_test(void)
{
	struct command_letter_test {
		int error;
		int thread;
		size_t num;
	};
	const struct {
		char *input;
		size_t bufsz;
		int error;
		const char *command;
		struct command_letter_test *letters;
		size_t nletter;
	} tests[] = {
		#define LETTER(error, thread, num) { error, thread, num }
		#define LETTERS(...) (struct command_letter_test []) { __VA_ARGS__ }, \
				 nitems(((struct command_letter_test []) { __VA_ARGS__ }))
		{ "read 1\n", 0, COMMAND_OK, "read", LETTERS(LETTER(0, 0, 1)) },
		{ "read t 1\n", 0, COMMAND_OK, "read", LETTERS(LETTER(0, 1, 1)) },
		{ "read t\n", 0, COMMAND_OK, "read", LETTERS(LETTER(COMMAND_THREAD_EOF, 0, 0)) },
	};
	size_t i;

	for (i = 0; i < nitems(tests); i++) {
		struct command_letter letter;
		struct command_lexer lex;
		FILE *fp;
		size_t bufsz, j;
		int error;
		char buf[128];

		if ((fp = fmemopen(tests[i].input, strlen(tests[i].input), "r")) == NULL)
			err(1, "fmemopen");

		command_init(&lex, fp);

		if ((bufsz = tests[i].bufsz) == 0)
			bufsz = sizeof(buf);

		error = command_name(&lex, buf, bufsz);
		if (error != tests[i].error)
			errx(1, "wrong error");
		if (error != COMMAND_OK)
			goto done;
		if (strcmp(buf, tests[i].command) != 0)
			errx(1, "wrong command");

		for (j = 0; j < tests[i].nletter; j++) {
			error = command_letter(&lex, &letter);
			if (error != tests[i].letters[j].error)
				errx(1, "wrong error");
			if (error != COMMAND_OK)
				goto done;
			if (letter.thread != tests[i].letters[j].thread)
				errx(1, "wrong thread boolean value");
			if (letter.num != tests[i].letters[j].num)
				errx(1, "wrong letter number");
		}

		if (command_letter(&lex, &letter) != COMMAND_EOF)
			errx(1, "wrong error");

		done:
		fclose(fp);
	}
}
