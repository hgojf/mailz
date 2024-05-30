#include <sys/tree.h>
#include <sys/wait.h>

#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "mail.h"
#include "maildir.h"
#include "util.h"

struct command {
	const char *ident;
	int (*fn) (struct maildir *, struct options *, char *);
};

static int argv_ify(char *, size_t *, char ***);

static int ignore(struct maildir *, struct options *, char *);
static int unignore(struct maildir *, struct options *, char *);
static int more(struct maildir *, struct options *, char *);
static int print(struct maildir *, struct options *, char *);
static int reorder(struct maildir *, struct options *, char *);
static int see(struct maildir *, struct options *, char *);
static int thread(struct maildir *, struct options *, char *);
static int unsee(struct maildir *, struct options *, char *);

static struct command commands[] =
{
	{ "ignore", ignore },
	{ "more", more },
	{ "p", print },
	{ "r", see },
	{ "reorder", reorder },
	{ "t", thread },
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
	return argv_ify(args, &options->nignore, &options->ignore);
}

static int
unignore(__unused struct maildir *maildir, struct options *options, char *args)
{
	return argv_ify(args, &options->nunignore, &options->unignore);
}

static int
more(struct maildir *maildir, struct options *options, char *args)
{
	pid_t pid;
	struct maildir_letter *letter;
	int p[2];
	FILE *fp;

	if (args != NULL) {
		const char *errstr;
		size_t idx;

		idx = strtosize(args, 1, maildir->nletters, &errstr);
		if (errstr != NULL) {
			warnx("Message number was %s", errstr);
			return -1;
		}
		options->msg = idx;
	}

	letter = &maildir->letters[options->msg - 1];

	if (pipe2(p, O_CLOEXEC) == -1)
		return -1;
	if ((fp = fdopen(p[1], "w")) == NULL) {
		close(p[0]);
		close(p[1]);
		return -1;
	}


	switch (pid = fork()) {
	case -1:
		fclose(fp);
		return -1;
	case 0:
		close(p[1]);
		if (dup2(p[0], STDIN_FILENO) == -1)
			err(1, "dup2");
		close(p[0]);
		execl("/usr/local/libexec/lesswrapper", "lesswrapper", "-", NULL);
		err(1, "execl");
		/* NOTREACHED */
	default:
		break;
	}

	signal(SIGPIPE, SIG_IGN);
	close(p[0]);
	maildir_letter_print_read(maildir, letter, options, fp);
	fflush(fp);
	fclose(fp);
	waitpid(pid, NULL, 0);
	signal(SIGPIPE, SIG_DFL);
	return 0;
}

static int
reorder(__unused struct maildir *maildir, struct options *options, char *args)
{
	return argv_ify(args, &options->nreorder, &options->reorder);
}

static int
see(struct maildir *maildir, struct options *options, char *args)
{
	struct maildir_letter *letter = &maildir->letters[options->msg - 1];

	if (args != NULL) {
		const char *errstr;
		size_t idx;

		idx = strtosize(args, 1, maildir->nletters, &errstr);
		if (errstr != NULL) {
			warnx("Message number was %s", errstr);
			return -1;
		}
		letter = &maildir->letters[idx - 1];
	}

	if (maildir_letter_set_flag(maildir, letter, 'S') == -1)
		return -1;
	return 0;
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

static int
print(struct maildir *maildir, __unused struct options *options, char *args)
{
	char *start, *end;
	const char *errstr;
	size_t b, e;

	if ((start = strsep(&args, "-")) != NULL) {
		b = strtosize(start, 1, maildir->nletters, &errstr);
		if (errstr != NULL) {
			printf("range was %s\n", errstr);
			return -1;
		}
		if ((end = strsep(&args, "-")) != NULL) {
			e = strtosize(end, b, maildir->nletters, &errstr);
			if (errstr != NULL) {
				printf("range was %s\n", errstr);
				return -1;
			}
		}
		else
			e = maildir->nletters;
		b -= 1;
	}
	else {
		b = 0;
		e = maildir->nletters;
	}

	return maildir_print(maildir, b, e);
}

static int
thread(struct maildir *maildir, __unused struct options *options, char *args)
{
	struct maildir_letter *letter;
	const char *subject;
	int re;
	size_t start;

	if (args != NULL) {
		const char *errstr;
		size_t idx;

		idx = strtosize(args, 1, maildir->nletters, &errstr);
		if (errstr != NULL) {
			warnx("Message number was %s", errstr);
			return -1;
		}
		options->msg = idx;
	}

	letter = &maildir->letters[options->msg - 1];
	subject = letter->subject;
	re = 0;
	if (!strncmp(letter->subject, "Re: ", strlen("Re: "))) {
		subject += strlen("Re: ");
		re = 1;
	}

	/* if this is the first message in a chain,
	 * dont search mails before it 
	 */
	if (!re) {
		start = options->msg - 1;
		/* wont be printed in loop */
		if (maildir_letter_print(options->msg, letter) == -1)
			return -1;
	}
	else
		start = 0;

	for (size_t i = start; i < maildir->nletters; i++) {
		struct maildir_letter *l = &maildir->letters[i];

		if ((!strncmp(l->subject, "Re: ", strlen("Re: ")) && 
			!strcmp(l->subject + strlen("Re: "), subject)) ||
			(re && !strcmp(l->subject, subject))) 
		{
			if (maildir_letter_print(i + 1, l) == -1)
				return -1;
		}
	}

	return 0;
}

static int
argv_ify(char *args, size_t *out_argc, char ***out_argv)
{
	char **argv, *s, *p;
	size_t argc;
	void *t;
	int rv = -1;

	argv = *out_argv;
	argc = *out_argc;

	while ((s = strsep(&args, " \t")) != NULL) {
		if ((p = strdup(s)) == NULL)
			goto fail;
		t = reallocarray(argv, argc + 1, sizeof(*argv));
		if (t == NULL) {
			free(p);
			goto fail;
		}

		argv = t;
		argv[argc] = p;
		argc++;
	}

	rv = 0;
	fail:
	*out_argc = argc;
	*out_argv = argv;
	return rv;
}
