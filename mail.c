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

#include <sys/stat.h>
#include <sys/wait.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "mail.h"
#include "mailbox.h"
#include "pathnames.h"
#include "reallocarray.h"
#include "sendmail.h"
#include "strsep.h"
#include "strtonum.h"

#ifndef __unused
#if defined(__GNUC__) || defined(__clang__)
#define __unused __attribute__((unused))
#else
#define __unused /* delete */
#endif /* __GNUC__ || __clang__ */
#endif /* __unused */

struct command {
	const char *ident;
	const char *usage;
	#define COMMAND_FATAL -3
	#define COMMAND_USAGE -2
	#define COMMAND_ERROR -1
	#define COMMAND_OK 0
	int (*fn) (struct mailbox *, struct options *, char *);
};

static int argv_ify(char *, size_t *, char ***);
static int command_run(char *, struct mailbox *, struct options *);
static int command_cmp(const void *, const void *);
static int configure(struct mailbox *, struct options *);
static const char *config_location(void);
static void options_free(struct options *);
static void usage(void);

static int ignore(struct mailbox *, struct options *, char *);
static int unignore(struct mailbox *, struct options *, char *);
static int more(struct mailbox *, struct options *, char *);
static int print(struct mailbox *, struct options *, char *);
static int reorder(struct mailbox *, struct options *, char *);
static int reply(struct mailbox *, struct options *, char *);
static int save(struct mailbox *, struct options *, char *);
static int see(struct mailbox *, struct options *, char *);
static int set(struct mailbox *, struct options *, char *);
static int send(struct mailbox *, struct options *, char *);
static int thread(struct mailbox *, struct options *, char *);
static int unsee(struct mailbox *, struct options *, char *);

static struct command commands[] =
{
	{ "ignore", "[headers...]", ignore },
	{ "more", "[message number]", more },
	{ "p", "", print },
	{ "r", "[message number]", see },
	{ "reorder", "[headers...]", reorder },
	{ "reply", "[message number]", reply },
	{ "s", "[message number]", save },
	{ "send", "[message number]", send },
	{ "set", "variable [value]", set },
	{ "t", "[message number]", thread },
	{ "unignore", "[headers...]", unignore },
	{ "x", "", unsee },
};

#define max(a, b) ((a) > (b) ? (a) : (b))
#define nitems(a) (sizeof((a)) / sizeof((*a)))

int
main(int argc, char *argv[])
{
	int ch, fd, rv;
	char *line = NULL;
	const char *subject;
	size_t n = 0;
	ssize_t len;
	struct options options;
	const char *cfg;
	struct mailbox mailbox;

	rv = 1;

	memset(&options, 0, sizeof(options));
	options.msg = 1;

	subject = NULL;

	while ((ch = getopt(argc, argv, "as:")) != -1) {
		switch (ch) {
		case 's':
			subject = optarg;
			break;
		case 'a': /* 'all' */
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

	if (mkdir("/tmp/mail/", 0700) == -1 && errno != EEXIST)
		err(1, "mkdir");
	if (unveil("/tmp/mail/", "crw") == -1)
		err(1, "unveil");
	if ((cfg = config_location()) != NULL && unveil(cfg, "r") == -1)
		err(1, "unveil");
	if (unveil(PATH_MAILZWRAPPER, "x") == -1)
		err(1, "unveil");
	if (pledge("stdio rpath cpath wpath proc exec unveil", NULL) == -1)
		err(1, "pledge");

	if (subject != NULL) {
		struct sendmail letter;
		size_t tl;

		if (pledge("stdio rpath cpath wpath proc exec", NULL) == -1)
			err(1, "pledge");

		mailbox.nletters = 0;
		mailbox.letters = NULL;
		mailbox.type = MAILBOX_MAILDIR;
		mailbox.val.mbox_file = NULL;
		/* only mailbox.nletters = 0 should be necessary */

		if (configure(&mailbox, &options) == -1) {
			options_free(&options);
			return 1;
		}

		if (options.address == NULL || options.name == NULL) {
			(void) fputs("Must set an email address and real name\n", stderr);
			options_free(&options);
			return 1;
		}

		letter.from.addr = options.address;
		letter.from.name = options.name;

		letter.subject = subject;
		letter.to = argv[0];

		tl = strlen(argv[0]);
		if (tl > INT_MAX)
			errx(1, "Email address is too long");
		letter.tl = (int) tl;

		letter.re = 0;
		letter.seed = NULL;

		rv = sendmail(EDIT_CAT, &letter) == -1 ? 1 : 0;
		options_free(&options);

		return rv;
	}
	/* else */

	if (unveil(argv[0], "rc") == -1)
		err(1, "unveil");

	if ((fd = open(argv[0], O_RDONLY | O_CLOEXEC)) == -1)
		err(1, "open %s", argv[0]);
	if (mailbox_setup(fd, &mailbox) == -1)
		err(1, "mailbox_setup");

	/* dont need to reopen this ever */
	if (mailbox.type == MAILBOX_MBOX && unveil(argv[0], "") == -1)
		err(1, "unveil");
	/* only need 'cur' directory for maildir */
	if (mailbox.type == MAILBOX_MAILDIR) {
		char path[PATH_MAX];
		int n;

		n = snprintf(path, sizeof(path), "%s/cur", argv[0]);
		if (n < 0)
			err(1, "snprintf");
		/* 
		 * could happen because earlier accesses use *at functions 
		 * instead of the entire path
		 */
		if ( (size_t) n >= sizeof(path))
			errx(1, "snprintf truncation");
		if (unveil(path, "crw") == -1)
			err(1, "unveil");
		if (unveil(argv[0], "") == -1)
			err(1, "unveil");
	}
	if (pledge("stdio rpath cpath wpath proc exec", NULL) == -1)
		err(1, "pledge");

	if (mailbox_read(&mailbox, options.view_seen) == -1)
		err(1, "mailbox_read");

	if (mailbox.nletters == 0) {
		if (puts("No mail.") == EOF)
			goto fail;
		goto good;
	}

	if (configure(&mailbox, &options) == -1)
		goto fail;

	if (mailbox_print(&mailbox, 0, mailbox.nletters) == -1)
		goto fail;

	if (fputs("> ", stdout) == EOF)
		goto fail;
	while ((len = getline(&line, &n, stdin)) != -1) {
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		if (*line == '\0') {
			if (options.msg == mailbox.nletters) {
				if (printf("No more messages\n") < 0)
					goto fail;
				if (fputs("> ", stdout) == EOF)
					goto fail;
				continue;
			}
			if (mailbox_letter_print_read(&mailbox, &mailbox.letters[options.msg++ - 1],
					&options, stdout) == -1)
				goto fail;
		}
		else if (isdigit(*line)) {
			const char *errstr;
			long long msg;

			msg = strtonum(line, 1, mailbox.nletters, &errstr);
			if (errstr != NULL) {
				if (fprintf(stderr, "message number was %s\n", errstr) < 0)
					goto fail;
			}
			else {
				options.msg = msg;
				if (mailbox_letter_print_read(&mailbox, &mailbox.letters[options.msg - 1],
						&options, stdout) == -1)
					goto fail;
			}
		}
		else {
			struct command *cmd;
			const char *command;
			char *args;
			int cv;

			args = line;
			if ((command = strsep(&args, " \t")) == NULL)
				return -1;
			cmd = bsearch(command, commands, nitems(commands), 
			sizeof(*commands), command_cmp);
			if (cmd == NULL) {
				if (fprintf(stderr, "unknown command %s\n", command) < 0)
					goto fail;
				continue;
			}
			cv = cmd->fn(&mailbox, &options, args);

			switch (cv) {
			case COMMAND_USAGE:
				if (fprintf(stderr, "usage: %s %s\n", cmd->ident,
						cmd->usage) < 0)
					goto fail;
				break;
			case COMMAND_ERROR:
				if (fprintf(stderr, "command failed\n") < 0)
					goto fail;
				break;
			case COMMAND_OK:
				break;
			}
		}
		if (fputs("> ", stdout) == EOF)
			goto fail;
	}

	if (putchar('\n') == EOF)
		goto fail;

	good:
	rv = 0;
	fail:
	if (pledge("stdio cpath", NULL) == -1)
		err(1, "pledge");
	if (rmdir("/tmp/mail") == -1 && errno != ENOTEMPTY) {
		warn("rmdir");
		rv = 1;
	}
	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");
	free(line);
	mailbox_free(&mailbox);
	options_free(&options);
	return rv;
}

static void
options_free(struct options *options)
{
	free(options->address);
	free(options->name);
	for (size_t i = 0; i < options->nreorder; i++) {
		free(options->reorder[i]);
	}
	free(options->reorder);
	for (size_t i = 0; i < options->nignore; i++) {
		free(options->ignore[i]);
	}
	free(options->ignore);
	for (size_t i = 0; i < options->nunignore; i++) {
		free(options->unignore[i]);
	}
	free(options->unignore);
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

	rv = -1;

	if ((mailrc = config_location()) == NULL)
		return 0;
	if ((fp = fopen(mailrc, "re")) == NULL) {
		if (errno != ENOENT) {
			warn("fopen %s", mailrc);
			return -1;
		}
		return 0;
	}

	while ((len = getline(&line, &n, fp)) != -1) {
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		if (command_run(line, mailbox, options) == -1)
			goto fail;
	}

	rv = ferror(fp) ? -1 : 0;

	fail:
	free(line);
	if (fclose(fp) == EOF)
		rv = -1;
	return rv;
}

static void
usage(void)
{
	(void) fprintf(stderr, "usage: mailz [-s] <mailbox>\n");
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
	char path[] = "/tmp/mail/more.XXXXXXXXXX";
	int fd, rv;
	FILE *fp;

	if (args != NULL) {
		const char *errstr;
		long long idx;

		idx = strtonum(args, 1, mailbox->nletters, &errstr);
		if (errstr != NULL) {
			if (fprintf(stderr, "message number was %s\n", errstr) < 0)
				return COMMAND_FATAL;
			return COMMAND_USAGE;
		}
		options->msg = idx;
	}

	letter = &mailbox->letters[options->msg - 1];

	if ((fd = mkstemp(path)) == -1)
		return -1;
	if ((fp = fdopen(fd, "w")) == NULL) {
		(void) close(fd);
		(void) unlink(path);
		return -1;
	}

	switch (pid = fork()) {
	case -1:
		fclose(fp);
		return -1;
	case 0:
		execl(PATH_MAILZWRAPPER, "less", path, NULL);
		err(1, "execl");
		/* NOTREACHED */
	default:
		break;
	}

	rv = 0;
	if (mailbox_letter_print_read(mailbox, letter, options, fp) == -1)
		rv = -1;
	if (fclose(fp) == EOF)
		rv = -1;
	if (waitpid(pid, NULL, 0) == -1)
		rv = -1;
	if (unlink(path) == -1)
		rv = -1;
	return rv;
}

static int
reorder(__unused struct mailbox *mailbox, struct options *options, char *args)
{
	return argv_ify(args, &options->nreorder, &options->reorder);
}

static int
reply(struct mailbox *mailbox, struct options *options, char *args)
{
	struct letter *letter = &mailbox->letters[options->msg - 1];
	struct sendmail send;
	int fd;
	FILE *fp;
	int rv;
	char path[] = "/tmp/mail/reply.XXXXXX";
	struct from from;

	rv = -1;

	if (options->address == NULL || options->name == NULL) {
		if (fprintf(stderr, "must set an email address and real name\n") < 0)
			return COMMAND_FATAL;
		return -1;
	}

	if (args != NULL) {
		const char *errstr;
		long long idx;

		idx = strtonum(args, 1, mailbox->nletters, &errstr);
		if (errstr != NULL) {
			if (fprintf(stderr, "message number was %s\n", errstr) < 0)
				return COMMAND_FATAL;
			return COMMAND_USAGE;
		}
		letter = &mailbox->letters[idx - 1];
	}

	if (from_extract(letter->from, &from) == -1)
		return -1;

	if ((fd = mkstemp(path)) == -1)
		return -1;
	if (unlink(path) == -1) {
		(void) close(fd);
		return -1;
	}
	if ((fp = fdopen(fd, "a+")) == NULL) {
		(void) close(fd);
		return -1;
	}
	if (mailbox_letter_print_content(mailbox, letter, fp) == -1)
		goto fail;
	if (fseek(fp, 0, SEEK_SET) == -1)
		goto fail;

	send.seed = fp;

	send.from.addr = options->address;
	send.from.name = options->name;
	send.tl = strlen(from.addr) - 1;
	send.to = from.addr;

	send.subject = letter->subject;
	send.re = 1;
	if (letter->subject != NULL && strncmp(letter->subject, "Re: ", 4) == 0) {
		send.subject += 4;
	}

	if (sendmail(options->edit_mode, &send) == -1)
		goto fail;

	rv = 0;
	fail:
	if (fclose(fp) == EOF)
		rv = -1;
	return rv;
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
			if (fprintf(stderr, "message number was %s\n", errstr) < 0)
				return COMMAND_FATAL;
			return COMMAND_USAGE;
		}
		letter = &mailbox->letters[idx - 1];
	}

	if (mailbox_letter_mark_read(mailbox, letter) == -1)
		return -1;
	return 0;
}

static int
set(__unused struct mailbox *mailbox, struct options *options, char *args)
{
	const char *var, *val;
	char *orig;

	if ((var = strsep(&args, " \t")) == NULL) {
		return COMMAND_USAGE;
	}
	val = args;

	if (strcmp(var, "address") == 0) {
		if (val == NULL) {
			return COMMAND_USAGE;
		}
		orig = options->address;
		if ((options->address = strdup(val)) == NULL) {
			warn("strdup");
			return -1;
		}
		free(orig);
	}
	else if (strcmp(var, "linewrap") == 0) {
		unsigned int lr;
		const char *errstr;

		if (val != NULL) {
			lr = strtonum(val, 0, UINT_MAX, &errstr);
			if (errstr != NULL) {
				if (fprintf(stderr, "linewrap was %s\n", errstr) < 0)
					return COMMAND_FATAL;
				return COMMAND_USAGE;
			}
		}
		else
			lr = 72;
		options->linewrap = lr;
	}
	else if (strcmp(var, "name") == 0) {
		if (val == NULL) {
			if (fputs("need a value\n", stderr) == EOF)
				return COMMAND_FATAL;
			return COMMAND_USAGE;
		}
		orig = options->name;
		if ((options->name = strdup(val)) == NULL) {
			warn("strdup");
			return -1;
		}
		free(orig);
	}
	else if (strcmp(var, "edit") == 0) {
		if (strcmp(val, "vi") == 0)
			options->edit_mode = EDIT_VI;
		else if (strcmp(val, "manual") == 0)
			options->edit_mode = EDIT_MANUAL;
		else {
			if (fputs("unknown edit mode\n", stderr) == EOF)
				return COMMAND_FATAL;
			return COMMAND_USAGE;
		}
	}
	else {
		if (fputs("unknown variable\n", stderr) == EOF)
			return COMMAND_FATAL;
		return COMMAND_USAGE;
	}

	return 0;
}

static int
send(__unused struct mailbox *mailbox, struct options *options, char *args)
{
	struct sendmail letter;

	if (options->address == NULL || options->name == NULL) {
		if (fputs("must set an email address and real name\n", stderr) == EOF)
			return COMMAND_FATAL;
		return -1;
	}

	if ((letter.to = strsep(&args, " \t")) == NULL) {
		return COMMAND_USAGE;
	}
	letter.tl = strlen(letter.to);
	letter.from.addr = options->address;
	letter.from.name = options->name;
	letter.subject = args;
	letter.re = 0;
	letter.seed = NULL;

	return sendmail(options->edit_mode, &letter);
}

static int
save(struct mailbox *mailbox, struct options *options, char *args)
{
	struct letter *letter = &mailbox->letters[options->msg - 1];
	char path[] = "/tmp/mail/letter.XXXXXX";
	int fd, rv;
	FILE *fp;

	if (args != NULL) {
		const char *errstr;
		long long idx;

		idx = strtonum(args, 1, mailbox->nletters, &errstr);
		if (errstr != NULL) {
			if (fprintf(stderr, "message number was %s\n", errstr) < 0)
				return COMMAND_FATAL;
			return COMMAND_USAGE;
		}
		letter = &mailbox->letters[idx - 1];
	}

	if ((fd = mkstemp(path)) == -1) {
		warn("mktemp");
		return -1;
	}
	if ((fp = fdopen(fd, "w")) == NULL) {
		warn("fdopen");
		(void) close(fd);
		(void) unlink(path);
		return -1;
	}

	rv = 0;
	if (mailbox_letter_print_read(mailbox, letter, options, fp) == -1)
		rv = -1;
	else if (printf("saved letter to %s\n", path) < 0)
		rv = -1;
	if (fclose(fp) == EOF)
		rv = -1;
	return rv;
}

static int
unsee(struct mailbox *mailbox, struct options *options, char *args)
{
	struct letter *letter = &mailbox->letters[options->msg - 1];

	if (args != NULL) {
		return COMMAND_USAGE;
	}

	if (mailbox_letter_mark_unread(mailbox, letter) == -1)
		return -1;
	return 0;
}

static int
print(struct mailbox *mailbox, __unused struct options *options, char *args)
{
	if (args != NULL) {
		return COMMAND_USAGE;
	}
	return mailbox_print(mailbox, 0, mailbox->nletters);
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
			if (fprintf(stderr, "message number was %s\n", errstr) < 0)
				return COMMAND_FATAL;
			return COMMAND_USAGE;
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
