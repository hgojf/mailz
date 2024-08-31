#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"
#include "conf.h"
#include "letter.h"
#include "list.h"
#include "mark.h"
#include "print.h"
#include "send.h"

struct command {
	#define CMD_TYPE_LETTER 0
	int type;
	#define CMD_LETTER_ADDR 0
	#define CMD_LETTER_LETTER 1
	#define CMD_LETTER_LETTERS 2
	#define CMD_LETTER_PRINT 3
	#define CMD_LETTER_REPLY 4
	int letter_type;

	#define CMD_NOALIAS '\0'
	char alias;
	const char *ident;
	union {
		int (*addr) (int, struct address *, struct letter *);
		int (*letter) (int, struct letter *);
		int (*letters) (struct letter *, struct letter *, size_t);
		int (*print) (int, struct letter *, struct ignore *, struct reorder *, int);
		int (*reply) (int, struct address *, struct letter *);
	} fn;
};

static int command_cmp(const void *, const void *);

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

static const struct command commands[] = {
	{ CMD_TYPE_LETTER, CMD_LETTER_PRINT , CMD_NOALIAS, "more", .fn.print = page_letter },
	{ CMD_TYPE_LETTER, CMD_LETTER_LETTER, 'r', "read", .fn.letter = mark_read },
	{ CMD_TYPE_LETTER, CMD_LETTER_REPLY, CMD_NOALIAS, "reply", .fn.reply = reply },
	{ CMD_TYPE_LETTER, CMD_LETTER_PRINT, 's', "save", .fn.print = save_letter },
	{ CMD_TYPE_LETTER, CMD_LETTER_LETTERS, 't', "thread", .fn.letters = thread_print },
	{ CMD_TYPE_LETTER, CMD_LETTER_LETTER, 'x', "unread", .fn.letter = mark_unread },
};

int
commands_run(int cur, struct letter *letters, size_t nletter, 
	struct mailz_conf *conf)
{
	struct letter *letter;
	char *line;
	size_t n;

	if (list_letters(letters, 0, nletter) == -1)
		return -1;

	letter = NULL;
	line = NULL;
	n = 0;

	for (;;) {
		struct command *cmd;
		char *args, *arg0;
		ssize_t len;
		int crv;

		fputs("> ", stdout);
		if ((len = getline(&line, &n, stdin)) == -1)
			break;
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';

		args = line;

		if (*args == '\0')
			continue;

		/* args is non-NULL so this will always return non-NULL */
		arg0 = strsep(&args, " \t");

		cmd = bsearch(arg0, commands, nitems(commands), 
			sizeof(*commands), command_cmp);
		if (cmd == NULL) {
			warnx("unknown command '%s'", arg0);
			continue;
		}

		switch (cmd->type) {
		case CMD_TYPE_LETTER: {
			const char *errstr;
			size_t idx;

			if (args == NULL) {
				if (letter == NULL) {
					warnx("no letter specified and no previous letter");
					continue;
				}
				idx = (size_t)(letter - letters);
			}
			else {
				idx = strtonum(args, 1, nletter, &errstr);
				if (errstr != NULL) {
					warnx("message number '%s' was %s", args, errstr);
					continue;
				}
				idx -= 1;
			}

			switch (cmd->letter_type) {
			case CMD_LETTER_ADDR:
				crv = cmd->fn.addr(cur, &conf->address, &letters[idx]);
				break;
			case CMD_LETTER_LETTER:
				crv = cmd->fn.letter(cur, &letters[idx]);
				break;
			case CMD_LETTER_LETTERS:
				crv = cmd->fn.letters(&letters[idx], letters, nletter);
				break;
			case CMD_LETTER_PRINT:
				crv = cmd->fn.print(cur, &letters[idx], &conf->ignore, &conf->reorder,
					conf->linewrap);
				break;
			case CMD_LETTER_REPLY:
				crv = cmd->fn.reply(cur, &conf->address, &letters[idx]);
				break;
			default:
				/* NOTREACHED */
				abort();
			}

			if (crv != -1)
				letter = &letters[idx];
			break;
		}
		default:
			/* NOTREACHED */
			abort();
		}

		if (crv == -1) {
			warnx("command '%s' failed", cmd->ident);
			continue;
		}
	}

	putchar('\n');

	free(line);
	return 0;
}

static int
command_cmp(const void *one, const void *two)
{
	const char *n1 = one;
	const struct command *n2 = two;

	if (n2->alias != CMD_NOALIAS && strlen(n1) == 1) {
		if (*n1 > n2->alias)
			return 1;
		else if (*n1 == n2->alias)
			return 0;
		else
			return -1;
	}

	return strcmp(n1, n2->ident);
}
