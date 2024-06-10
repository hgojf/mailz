#include <sys/stat.h>
#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mail.h"
#include "mailbox.h"

struct command {
	const char *ident;
	int (*fn) (struct mailbox *, struct options *, char *);
};

static int argv_ify(char *, size_t *, char ***);
static int command_run(char *, struct mailbox *, struct options *);
static int command_cmp(const void *, const void *);
static int configure(struct mailbox *, struct options *);
static const char *config_location(void);
static void usage(void);

static int ignore(struct mailbox *, struct options *, char *);
static int unignore(struct mailbox *, struct options *, char *);
static int more(struct mailbox *, struct options *, char *);
static int print(struct mailbox *, struct options *, char *);
static int reorder(struct mailbox *, struct options *, char *);
static int see(struct mailbox *, struct options *, char *);
static int thread(struct mailbox *, struct options *, char *);
static int unsee(struct mailbox *, struct options *, char *);

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

#define max(a, b) ((a) > (b) ? (a) : (b))
#define nitems(a) (sizeof((a)) / sizeof((*a)))

int
main(int argc, char *argv[])
{
	int ch, fd;
	char *line = NULL;
	size_t n = 0;
	ssize_t len;
	struct options options;
	const char *cfg;
	struct mailbox mailbox;

	options.view_seen = 0;
	options.nignore = 0;
	options.ignore = NULL;
	options.nunignore = 0;
	options.unignore = NULL;
	options.nreorder = 0;
	options.reorder = NULL;
	options.msg = 1;

	while ((ch = getopt(argc, argv, "s")) != -1) {
		switch (ch) {
		case 's':
			options.view_seen = 1;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	if (unveil(argv[0], "rc") == -1)
		err(1, "unveil");
	if ((cfg = config_location()) != NULL && unveil(cfg, "r") == -1)
		err(1, "unveil");
	if (unveil("/usr/local/libexec/lesswrapper", "x") == -1)
		err(1, "unveil");
	if (pledge("stdio rpath cpath proc exec", NULL) == -1)
		err(1, "pledge");

	if ((fd = open(argv[0], O_RDONLY | O_CLOEXEC)) == -1)
		err(1, "open %s", argv[0]);
	if (mailbox_setup(fd, &mailbox) == -1)
		err(1, "mailbox_setup");
	if (mailbox.type == MAILBOX_MBOX) {
		if (pledge("stdio rpath proc exec", NULL) == -1)
			err(1, "pledge");
	}
	if (mailbox_read(&mailbox, &options) == -1)
		err(1, "mailbox_read");

	if (mailbox.nletters == 0) {
		puts("No mail.");
		return 0;
	}

	configure(&mailbox, &options);

	if (mailbox.type == MAILBOX_MBOX) {
		if (pledge("stdio proc exec", NULL) == -1)
			err(1, "pledge");
	}

	mailbox_print(&mailbox, 0, mailbox.nletters);

	fputs("> ", stdout);
	while ((len = getline(&line, &n, stdin)) != -1) {
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		if (*line == '\0') {
			if (options.msg == mailbox.nletters) {
				printf("No more messages\n");
				fputs("> ", stdout);
				continue;
			}
			mailbox_letter_print_read(&mailbox, &mailbox.letters[options.msg++ - 1],
				&options, stdout);
		}
		else if (isdigit(*line)) {
			const char *errstr;

			options.msg = strtonum(line, 1, mailbox.nletters, &errstr);
			if (errstr != NULL)
				warnx("Message number was %s", errstr);
			else {
				mailbox_letter_print_read(&mailbox, &mailbox.letters[options.msg - 1],
					&options, stdout);
			}
		}
		else {
			command_run(line, &mailbox, &options);
		}
		fputs("> ", stdout);
	}

	putchar('\n');

	free(line);
	close(fd);
	mailbox_free(&mailbox);
	for (size_t i = 0; i < options.nreorder; i++) {
		free(options.reorder[i]);
	}
	free(options.reorder);
	for (size_t i = 0; i < options.nignore; i++) {
		free(options.ignore[i]);
	}
	free(options.ignore);
	for (size_t i = 0; i < options.nunignore; i++) {
		free(options.unignore[i]);
	}
	free(options.unignore);
	assert(getdtablecount() == 3);
}

static const char *
config_location(void)
{
	static char path[PATH_MAX];
	const char *mailrc, *home;
	static const char *ret = NULL;
	int n;

	if (ret != NULL)
		return ret;

	if ((mailrc = getenv("MAILZRC")) != NULL) {
		ret = mailrc;
		return mailrc;
	}
	if ((home = getenv("HOME")) == NULL)
		return NULL;
	n = snprintf(path, PATH_MAX, "%s/.mailzrc", home);
	if (n < 0 || n >= PATH_MAX)
		return NULL;

	ret = path;

	return ret;
}

static int
configure(struct mailbox *mailbox, struct options *options)
{
	const char *mailrc;
	FILE *fp;
	char *line = NULL;
	size_t n = 0;
	ssize_t len;
	int rv;

	if ((mailrc = config_location()) == NULL)
		return 0;
	if ((fp = fopen(mailrc, "re")) == NULL) {
		warn("fopen %s", mailrc);
		return -1;
	}

	while ((len = getline(&line, &n, fp)) != -1) {
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		command_run(line, mailbox, options);
	}

	rv = ferror(fp) ? -1 : 0;
	free(line);
	fclose(fp);
	return rv;
}

static void
usage(void)
{
	fprintf(stderr, "usage: mailz [-s] <mailbox>\n");
	exit(1);
}

static int
command_cmp(const void *one, const void *two)
{
	const char *n1 = one;
	const struct command *n2 = two;

	return strcmp(n1, n2->ident);
}

int
command_run(char *args, struct mailbox *mailbox, struct options *options)
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
	return cmd->fn(mailbox, options, args);
}

static int
ignore(__unused struct mailbox *mailbox, struct options *options, char *args)
{
	return argv_ify(args, &options->nignore, &options->ignore);
}

static int
unignore(__unused struct mailbox *mailbox, struct options *options, char *args)
{
	return argv_ify(args, &options->nunignore, &options->unignore);
}

static int
more(struct mailbox *mailbox, struct options *options, char *args)
{
	pid_t pid;
	struct letter *letter;
	int p[2];
	FILE *fp;

	if (args != NULL) {
		const char *errstr;
		long long idx;

		idx = strtonum(args, 1, mailbox->nletters, &errstr);
		if (errstr != NULL) {
			warnx("Message number was %s", errstr);
			return -1;
		}
		options->msg = idx;
	}

	letter = &mailbox->letters[options->msg - 1];

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
	mailbox_letter_print_read(mailbox, letter, options, fp);
	fflush(fp);
	fclose(fp);
	waitpid(pid, NULL, 0);
	signal(SIGPIPE, SIG_DFL);
	return 0;
}

static int
reorder(__unused struct mailbox *mailbox, struct options *options, char *args)
{
	return argv_ify(args, &options->nreorder, &options->reorder);
}

static int
see(struct mailbox *mailbox, struct options *options, char *args)
{
	struct letter *letter = &mailbox->letters[options->msg - 1];

	if (args != NULL) {
		const char *errstr;
		long long idx;

		idx = strtonum(args, 1, mailbox->nletters, &errstr);
		if (errstr != NULL) {
			warnx("Message number was %s", errstr);
			return -1;
		}
		letter = &mailbox->letters[idx - 1];
	}

	if (mailbox_letter_mark_read(mailbox, letter) == -1)
		return -1;
	return 0;
}

static int
unsee(struct mailbox *mailbox, struct options *options, char *args)
{
	struct letter *letter = &mailbox->letters[options->msg - 1];

	if (args != NULL) {
		warnx("This command takes no arguments.");
		return 0;
	}

	if (mailbox_letter_mark_unread(mailbox, letter) == -1)
		return -1;
	return 0;
}

static int
print(struct mailbox *mailbox, __unused struct options *options, char *args)
{
	char *start, *end;
	const char *errstr;
	long long b, e;

	if ((start = strsep(&args, "-")) != NULL) {
		b = strtonum(start, 1, mailbox->nletters, &errstr);
		if (errstr != NULL) {
			printf("range was %s\n", errstr);
			return -1;
		}
		if ((end = strsep(&args, "-")) != NULL) {
			e = strtonum(end, b, mailbox->nletters, &errstr);
			if (errstr != NULL) {
				printf("range was %s\n", errstr);
				return -1;
			}
		}
		else
			e = mailbox->nletters;
		b -= 1;
	}
	else {
		b = 0;
		e = mailbox->nletters;
	}

	return mailbox_print(mailbox, b, e);
}

static int
thread(struct mailbox *mailbox, __unused struct options *options, char *args)
{
	struct letter *letter;
	const char *subject;
	int re;
	size_t start;

	if (args != NULL) {
		const char *errstr;
		long long idx;

		idx = strtonum(args, 1, mailbox->nletters, &errstr);
		if (errstr != NULL) {
			warnx("Message number was %s", errstr);
			return -1;
		}
		options->msg = idx;
	}

	letter = &mailbox->letters[options->msg - 1];

	if (letter->subject == NULL) {
		/* cant really find a thread without a subject */
		return mailbox_letter_print(1, letter);
	}

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
		if (mailbox_letter_print(options->msg, letter) == -1)
			return -1;
	}
	else
		start = 0;

	for (long long i = start; i < mailbox->nletters; i++) {
		struct letter *l = &mailbox->letters[i];

		if (l->subject == NULL)
			continue;

		if ((!strncmp(l->subject, "Re: ", strlen("Re: ")) && 
			!strcmp(l->subject + strlen("Re: "), subject)) ||
			(re && !strcmp(l->subject, subject))) 
		{
			if (mailbox_letter_print(i + 1, l) == -1)
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
