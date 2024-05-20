#include <sys/tree.h>
#include <sys/wait.h>

#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mail.h"
#include "maildir.h"

struct command {
	const char *ident;
	int (*fn) (struct maildir *, struct options *, char *);
};

static int ignore(struct maildir *, struct options *, char *);
static int unignore(struct maildir *, struct options *, char *);
static int more(struct maildir *, struct options *, char *);
static int unsee(struct maildir *, struct options *, char *);

static struct command commands[] =
{
	{ "ignore", ignore },
	{ "more", more },
	{ "unignore", unignore },
	{ "x", unsee },
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
command_run(char *args, struct maildir *maildir, struct options *options)
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
	return cmd->fn(maildir, options, args);
}

static int
ignore(__unused struct maildir *maildir, struct options *options, char *args)
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
unignore(__unused struct maildir *maildir, struct options *options, char *args)
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

static int
more(struct maildir *maildir, struct options *options, char *args)
{
	int tfd;
	pid_t pid;
	FILE *fp;
	struct maildir_letter *letter = &maildir->letters[options->msg - 1];
	char template[] = "/tmp/mailz/letter.XXXXXX";

	if (args != NULL) {
		warnx("This command takes no arguments.");
		return 0;
	}

	if ((tfd = mkstemp(template)) == -1)
		return -1;
	if ((fp = fdopen(tfd, "w")) == NULL) {
		close(tfd);
		return -1;
	}

	if (maildir_letter_print_read(maildir, letter, options, fp) == -1) {
		fclose(fp);
		return -1;
	}

	switch (pid = fork()) {
		case -1:
			fclose(fp);
			return -1;
		case 0:
			execl("/usr/bin/less", "less", "--", template, NULL);
			err(1, "execl");
			/* NOTREACHED */
		default:
			fclose(fp);
			waitpid(pid, NULL, 0);
			unlink(template);
			return 0;
	}
}

static int
unsee(struct maildir *maildir, struct options *options, char *args)
{
	struct maildir_letter *letter = &maildir->letters[options->msg - 1];

	if (args != NULL) {
		warnx("This command takes no arguments.");
		return 0;
	}

	if (maildir_letter_set_flag(maildir, letter, '\0') == -1)
		return -1;
	return 0;
}
