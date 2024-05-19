#include <sys/tree.h>

#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "mail.h"

struct command {
	const char *ident;
	int (*fn) (struct options *, char *);
};

static int ignore(struct options *, char *);
static int unignore(struct options *, char *);

static struct command commands[] =
{
	{ "ignore", ignore },
	{ "unignore", unignore },
};

#define nitems(a) (sizeof((a)) / sizeof((*a)))

static int
command_cmp(const void *one, const void *two)
{
	const char *n1 = one;
	const struct command *n2 = two;

	return strcmp(n1, n2->ident);
}

int
command_run(char *args, struct options *options)
{
	const struct command *cmd;
	char *command;

	if ((command = strsep(&args, " \t")) == NULL)
		return -1;
	cmd = bsearch(command, commands, nitems(commands), 
		sizeof(*commands), command_cmp);
	if (cmd == NULL) {
		warnx("unknown command %s", command);
		return -1;
	}
	return cmd->fn(options, args);
}

static int
ignore(struct options *options, char *args)
{
	char *arg;

	while ((arg = strsep(&args, " \t")) != NULL) {
		void *t;
		char *s;

		if ((s = strdup(arg)) == NULL)
			return -1;

		t = reallocarray(options->ignore, options->nignore + 1, 
			sizeof(*options->ignore));
		if (t == NULL) {
			free(s);
			return -1;
		}

		options->ignore = t;
		options->ignore[options->nignore] = s;
		options->nignore++;
	}
	return 0;
}

static int
unignore(struct options *options, char *args)
{
	char *arg;

	while ((arg = strsep(&args, " \t")) != NULL) {
		void *t;
		char *s;

		if ((s = strdup(arg)) == NULL)
			return -1;

		t = reallocarray(options->unignore, options->nunignore + 1, 
			sizeof(*options->unignore));
		if (t == NULL) {
			free(s);
			return -1;
		}

		options->unignore = t;
		options->unignore[options->nunignore] = s;
		options->nunignore++;
	}
	return 0;
}
