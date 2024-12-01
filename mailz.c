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

#include <sys/wait.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <time.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "_err.h"
#include "conf.h"
#include "content-proc.h"
#include "maildir.h"
#include "pathnames.h"

struct command_args {
	const char *addr;
	const char *tmpdir;
	struct mailz_ignore *ignore;
	struct letter *letters;
	size_t nletter;
	int cur;
	int null;
};

struct letter {
	time_t date;
	char *from;
	char *path;
	char *subject;
};

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

static void commands_run(struct letter *, size_t, int, int,
			 const char *, const char *,
			 struct mailz_ignore *);
static int commands_token(FILE *, char *, size_t, int *);
static const struct command *commands_search(const char *);
static int command_flag(struct letter *, struct command_args *, int,
			int);
static int command_more(struct letter *, struct command_args *);
static int command_read(struct letter *, struct command_args *);
static int command_reply1(struct letter *, struct command_args *, int);
static int command_reply(struct letter *, struct command_args *);
static int command_respond(struct letter *, struct command_args *);
static int command_save(struct letter *, struct command_args *);
static int command_thread(struct letter *, struct command_args *);
static int command_unread(struct letter *, struct command_args *);
static int content_proc_ex_ignore(struct content_proc *,
				  const struct mailz_ignore *);
static int letter_cmp(const void *, const void *);
static void letter_free(struct letter *);
static int letter_from_summary(struct letter *, const char *,
			       const struct content_summary *);
static int letter_print(size_t, struct letter *);
static int read_letters(int, int, int, struct letter **, size_t *);
static void usage(void);

static const struct command {
	const char *ident;
	#define CMD_NOALIAS '\0'
	int alias;
	int (*fn) (struct letter *, struct command_args *);
} commands[] = {
	{ "more",	CMD_NOALIAS,	command_more },
	{ "read",	'r',		command_read },
	{ "reply",	CMD_NOALIAS,	command_reply },
	{ "respond",	CMD_NOALIAS,	command_respond },
	{ "save",	's',		command_save },
	{ "thread",	't',		command_thread },
	{ "unread",	'x',		command_unread },
};

static void
commands_run(struct letter *letters, size_t nletter, int cur,
	     int null, const char *tmpdir, const char *addr,
	     struct mailz_ignore *ignore)
{
	struct command_args args;
	struct letter *letter;

	args.addr = addr;
	args.letters = letters;
	args.nletter = nletter;
	args.cur = cur;
	args.ignore = ignore;
	args.null = null;
	args.tmpdir = tmpdir;

	letter = NULL;
	for (;;) {
		const struct command *cmd;
		char buf[300];
		int ch, gotnl;

		printf("> ");
		fflush(stdout);

		gotnl = 0;
		if (commands_token(stdin, buf, sizeof(buf), &gotnl) == -1) {
			warnx("argument too long");
			goto bad;
		}

		if (strlen(buf) == 0 && feof(stdin))
			break;
		if (strlen(buf) == 0 && gotnl)
			continue;

		if ((cmd = commands_search(buf)) == NULL) {
			warnx("unknown command");
			goto bad;
		}

		if (gotnl) {
			if (letter == NULL) {
				warnx("no current letter");
				goto bad;
			}
			if (cmd->fn(letter, &args) == -1)
				goto bad;
		}

		while (!gotnl) {
			const char *errstr;
			size_t idx;

			if (commands_token(stdin, buf, sizeof(buf), &gotnl) == -1) {
				warnx("argument too long");
				goto bad;
			}

			idx = strtonum(buf, 1, nletter, &errstr);
			if (errstr != NULL) {
				warnx("message number was %s", errstr);
				goto bad;
			}

			letter = &letters[idx - 1];
			if (cmd->fn(&letters[idx - 1], &args) == -1)
				goto bad;
		}

		bad:
		if (!gotnl)
			while ((ch = fgetc(stdin)) != EOF && ch != '\n')
				;
	}

	printf("\n");
}

static int
commands_token(FILE *fp, char *buf, size_t buflen, int *gotnl)
{
	size_t n;

	if (buflen == 0)
		return -1;

	n = 0;
	for (;;) {
		int ch;

		if ((ch = fgetc(fp)) == EOF || ch == '\n') {
			*gotnl = 1;
			break;
		}

		if (n != 0 && (ch == ' ' || ch == '\t'))
			break;

		if (n == buflen - 1)
			return -1;
		buf[n++] = ch;
	}

	buf[n] = '\0';
	return 0;
}

static const struct command *
commands_search(const char *s)
{
	size_t i, len;

	len = strlen(s);
	for (i = 0; i < nitems(commands); i++) {
		if ((len == 1 && commands[i].alias == *s)
				|| !strcmp(s, commands[i].ident))
			return &commands[i];
	}
	return NULL;
}

static int
command_flag(struct letter *letter, struct command_args *args,
	     int flag, int set)
{
	const char *bufp;
	char buf[NAME_MAX], *new;

	if (set) {
		bufp = maildir_set_flag(letter->path, flag, buf,
					sizeof(buf));
	}
	else {
		bufp = maildir_unset_flag(letter->path, flag, buf,
					sizeof(buf));
	}

	if (bufp == NULL)
		return -1;
	if (bufp == letter->path)
		return 0;

	if ((new = strdup(bufp)) == NULL)
		return -1;

	if (renameat(args->cur, letter->path, args->cur, new) == -1) {
		free(new);
		return -1;
	}

	free(letter->path);
	letter->path = new;

	return 0;
}


static int
command_more(struct letter *letter, struct command_args *args)
{
	struct content_proc pr;
	struct content_letter lr;
	FILE *fp;
	int fd, p[2], rv;
	pid_t pid;

	rv = -1;

	if (content_proc_init(&pr, PATH_MAILZ_CONTENT, args->null) == -1)
		return -1;

	if (content_proc_ex_ignore(&pr, args->ignore) == -1)
		goto pr;

	if ((fd = openat(args->cur, letter->path,
			 O_RDONLY | O_CLOEXEC)) == -1)
		goto pr;
	if (content_letter_init(&pr, &lr, fd) == -1)
		goto pr;

	if (pipe2(p, O_CLOEXEC) == -1)
		goto lr;
	if ((fp = fdopen(p[1], "w")) == NULL) {
		close(p[1]);
		close(p[0]);
		goto lr;
	}

	switch (pid = fork()) {
	case -1:
		fclose(fp);
		close(p[0]);
		goto lr;
	case 0:
		if (dup2(p[0], STDIN_FILENO) == -1)
			_err(1, "dup2");
		execl(PATH_LESS, "less", NULL);
		_err(1, "%s", PATH_LESS);
	default:
		break;
	}
	close(p[0]);

	for (;;) {
		char buf[4];
		int n;

		if ((n = content_letter_getc(&lr, buf)) == -1)
			goto pid;
		if (n == 0)
			break;

		if (fwrite(buf, n, 1, fp) != 1) {
			if (ferror(fp) && errno == EPIPE)
				break;
			goto pid;
		}
	}

	if (command_read(letter, args) == -1)
		goto pid;

	rv = 0;
	pid:
	fclose(fp);
	waitpid(pid, NULL, 0);
	lr:
	content_letter_close(&lr);
	pr:
	content_proc_kill(&pr);
	return rv;
}

static int
command_read(struct letter *letter, struct command_args *args)
{
	return command_flag(letter, args, 'S', 1);
}

static int
command_reply(struct letter *letter, struct command_args *args)
{
	return command_reply1(letter, args, 1);
}

static int
command_reply1(struct letter *letter, struct command_args *args, int group)
{
	struct content_proc pr;
	char path[PATH_MAX];
	FILE *fp;
	pid_t pid;
	int ch, fd, lfd, n, rv, status;

	rv = -1;

	if (content_proc_init(&pr, PATH_MAILZ_CONTENT, args->null) == -1)
		return -1;

	n = snprintf(path, sizeof(path), "%s/reply.XXXXXX", args->tmpdir);
	if (n < 0 || (size_t)n >= sizeof(path))
		goto pr;

	if ((fd = mkostemp(path, O_CLOEXEC)) == -1)
		goto pr;
	if ((fp = fdopen(fd, "w")) == NULL) {
		unlink(path);
		close(fd);
		goto pr;
	}

	if ((lfd = openat(args->cur, letter->path, O_RDONLY | O_CLOEXEC)) == -1)
		goto fp;
	if (content_proc_reply(&pr, fp, args->addr, group, lfd) == -1)
		goto fp;

	if (fflush(fp) == EOF)
		goto fp;

	if (printf("message located at %s\n"
		   "press enter to send or q to cancel: ", path) < 0)
		goto fp;
	if (fflush(stdout) == EOF)
		goto fp;

	if ((ch = fgetc(stdin)) == EOF)
		goto fp;

	if (ch != '\n') {
		int any, c;

		any = 0;
		while ((c = fgetc(stdin)) == EOF && c != '\n')
			any = 1;
		if (ch == 'q' && !any)
			rv = 0;
		goto fp;
	}

	lfd = fileno(fp);
	if (lseek(lfd, 0, SEEK_SET) == -1)
		goto fp;
	switch (pid = fork()) {
	case -1:
		goto fp;
	case 0:
		if (dup2(lfd, STDIN_FILENO) == -1)
			_err(1, "dup2");
		execl(PATH_SENDMAIL, "sendmail", "-t", NULL);
		_err(1, "%s", PATH_SENDMAIL);
	default:
		break;
	}

	if (waitpid(pid, &status, 0) == -1)
		goto fp;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		goto fp;

	rv = 0;
	fp:
	fclose(fp);
	unlink(path);
	pr:
	content_proc_kill(&pr);
	return rv;
}

static int
command_respond(struct letter *letter, struct command_args *args)
{
	return command_reply1(letter, args, 0);
}

static int
command_save(struct letter *letter, struct command_args *args)
{
	struct content_proc pr;
	struct content_letter lr;
	char path[PATH_MAX];
	FILE *fp;
	int fd, lfd, n, rv;

	rv = -1;

	if (content_proc_init(&pr, PATH_MAILZ_CONTENT, args->null) == -1)
		return -1;

	if (content_proc_ex_ignore(&pr, args->ignore) == -1)
		goto pr;

	if ((lfd = openat(args->cur, letter->path,
			  O_RDONLY | O_CLOEXEC)) == -1)
		goto pr;
	if (content_letter_init(&pr, &lr, lfd) == -1)
		goto pr;

	n = snprintf(path, sizeof(path), "%s/save.XXXXXX", args->tmpdir);
	if (n < 0 || (size_t)n >= sizeof(path))
		goto lr;

	if ((fd = mkostemp(path, O_CLOEXEC)) == -1)
		goto lr;
	if ((fp = fdopen(fd, "w")) == NULL) {
		unlink(path);
		close(fd);
		goto lr;
	}

	for (;;) {
		char buf[4];
		int n;

		if ((n = content_letter_getc(&lr, buf)) == -1)
			goto fp;
		if (n == 0)
			break;

		if (fwrite(buf, n, 1, fp) != 1)
			goto fp;
	}

	if (fflush(fp) == EOF)
		goto fp;

	if (printf("message saved to %s\n", path) < 0)
		goto fp;

	rv = 0;
	fp:
	fclose(fp);
	if (rv == -1)
		unlink(path);
	lr:
	content_letter_close(&lr);
	pr:
	content_proc_kill(&pr);
	return rv;
}

static int
command_thread(struct letter *letter, struct command_args *args)
{
	const char *subject;
	size_t i, idx, start;
	int have_first;

	idx = letter - args->letters;

	if ((subject = letter->subject) == NULL) {
		if (letter_print(idx + 1, letter) == -1)
			return -1;
		return 0;
	}

	if ((have_first = strncmp(subject, "Re: ", 4))) {
		if (letter_print(idx + 1, letter) == -1)
			return -1;
		start = idx + 1;
	}
	else {
		subject += 4;
		start = 0;
	}

	for (i = start; i < args->nletter; i++) {
		if (args->letters[i].subject == NULL)
			continue;

		if (!strcmp(args->letters[i].subject, subject)) {
			if (have_first)
				break;
			if (letter_print(i + 1, &args->letters[i]) == -1)
				return -1;
			have_first = 1;
		}
		else if (!strncmp(args->letters[i].subject, "Re: ", 4)
			&& !strcmp(&args->letters[i].subject[4],
				   subject)) {
			if (letter_print(i + 1, &args->letters[i]) == -1)
				return -1;
		}
	}

	return 0;
}

static int
command_unread(struct letter *letter, struct command_args *args)
{
	return command_flag(letter, args, 'S', 0);
}

static int
content_proc_ex_ignore(struct content_proc *pr,
		       const struct mailz_ignore *ignore)
{
	size_t i;
	int type;

	if (ignore->type == MAILZ_IGNORE_IGNORE)
		type = CNT_IGNORE_IGNORE;
	else
		type = CNT_IGNORE_RETAIN;

	for (i = 0; i < ignore->nheader; i++)
		if (content_proc_ignore(pr, ignore->headers[i],
					type) == -1)
			return -1;
	return 0;
}

static int
letter_cmp(const void *one, const void *two)
{
	time_t n1, n2;

	n1 = ((const struct letter *)one)->date;
	n2 = ((const struct letter *)two)->date;

	if (n1 > n2)
		return 1;
	else if (n1 == n2)
		return 0;
	else
		return -1;
}

static void
letter_free(struct letter *letter)
{
	free(letter->from);
	free(letter->path);
	free(letter->subject);
}

static int
letter_from_summary(struct letter *letter, const char *path,
		    const struct content_summary *sm)
{
	if ((letter->from = strdup(sm->from)) == NULL)
		return -1;

	if ((letter->path = strdup(path)) == NULL)
		goto from;

	if (sm->have_subject) {
		if ((letter->subject = strdup(sm->subject)) == NULL)
			goto path;
	}
	else
		letter->subject = NULL;

	letter->date = sm->date;

	return 0;

	path:
	free(letter->path);
	from:
	free(letter->from);
	return -1;
}

static int
letter_print(size_t nth, struct letter *letter)
{
	struct tm tm;
	char date[33];
	const char *subject;

	if (localtime_r(&letter->date, &tm) == NULL)
		return -1;
	if (strftime(date, sizeof(date), "%a %b %d %H:%M", &tm) == 0)
		return -1;

	if ((subject = letter->subject) == NULL)
		subject = "No Subject";

	if (printf("%4zu %-20s %-32s %-30s\n", nth, date,
		   letter->from, subject) < 0)
		return -1;
	return 0;
}

static int
read_letters(int ocur, int view_all, int null,
	     struct letter **o_letters, size_t *o_nletter)
{
	DIR *cur;
	struct content_proc pr;
	struct letter *letters;
	size_t i, nletter;
	int curfd;

	if ((curfd = dup(ocur)) == -1)
		return -1;
	if (fcntl(curfd, F_SETFD, FD_CLOEXEC) == -1) {
		close(curfd);
		return -1;
	}
	if ((cur = fdopendir(curfd)) == NULL) {
		close(curfd);
		return -1;
	}

	if (content_proc_init(&pr, PATH_MAILZ_CONTENT, null) == -1)
		goto cur;

	letters = NULL;
	nletter = 0;
	for (;;) {
		struct content_summary sm;
		struct letter letter, *t;
		struct dirent *de;
		int fd;

		errno = 0;
		if ((de = readdir(cur)) == NULL) {
			if (errno == 0)
				break;
			goto letters;
		}

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;

		if (!view_all && maildir_get_flag(de->d_name, 'S'))
			continue;

		if (nletter == SIZE_MAX)
			goto letters;

		if ((fd = openat(curfd, de->d_name, O_RDONLY | O_CLOEXEC)) == -1)
			goto letters;
		if (content_proc_summary(&pr, &sm, fd) == -1)
			goto letters;

		if (letter_from_summary(&letter, de->d_name, &sm) == -1)
			goto letters;

		t = reallocarray(letters, nletter + 1, sizeof(*letters));
		if (t == NULL) {
			letter_free(&letter);
			goto letters;
		}

		letters = t;
		letters[nletter++] = letter;
	}

	qsort(letters, nletter, sizeof(*letters), letter_cmp);
	*o_letters = letters;
	*o_nletter = nletter;

	closedir(cur);
	content_proc_kill(&pr);
	return 0;

	letters:
	for (i = 0; i < nletter; i++)
		letter_free(&letters[i]);
	free(letters);
	content_proc_kill(&pr);
	cur:
	closedir(cur);
	return -1;
}

static int
setup_letters(int root, int cur)
{
	DIR *new;
	int newfd, rv;

	rv = -1;

	if ((newfd = openat(root, "new",
			    O_RDONLY | O_DIRECTORY | O_CLOEXEC)) == -1)
		return -1;
	if ((new = fdopendir(newfd)) == NULL) {
		close(newfd);
		return -1;
	}

	for (;;) {
		char name[NAME_MAX], *namep;
		struct dirent *de;

		errno = 0;

		if ((de = readdir(new)) == NULL) {
			if (errno == 0)
				break;
			goto new;
		}

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;

		if (strchr(de->d_name, ':') == NULL) {
			int n;

			n = snprintf(name, sizeof(name), "%s:2,", de->d_name);
			if (n < 0 || (size_t)n >= sizeof(name))
				goto new;
			namep = name;
		}
		else
			namep = de->d_name;

		if (renameat(newfd, de->d_name, cur, namep) == -1)
			goto new;
	}

	rv = 0;
	new:
	closedir(new);
	return rv;
}

static void
usage(void)
{
	fprintf(stderr, "usage: mailz [-a] mailbox\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	char tmpdir[] = PATH_TMPDIR;
	struct mailz_conf conf;
	struct letter *letters;
	size_t i, nletter;
	int ch, cur, null, root, rv, view_all;

	rv = 1;

	view_all = 0;
	while ((ch = getopt(argc, argv, "a")) != -1) {
		switch (ch) {
		case 'a':
			view_all = 1;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		return 1;
	signal(SIGPIPE, SIG_IGN);

	if (mailz_conf_init(&conf) == -1)
		return 1;

	if ((root = open(argv[0], O_RDONLY | O_DIRECTORY | O_CLOEXEC)) == -1)
		goto conf;
	if ((cur = openat(root, "cur", O_RDONLY | O_DIRECTORY | O_CLOEXEC)) == -1)
		goto root;

	if (mkdtemp(tmpdir) == NULL)
		goto cur;

	if ((null = open(PATH_DEV_NULL, O_RDONLY | O_CLOEXEC)) == -1)
		goto tmpdir;

	if (unveil(tmpdir, "rwc") == -1)
		goto null;
	if (unveil(argv[0], "rc") == -1)
		goto null;
	if (unveil(PATH_LESS, "x") == -1)
		goto null;
	if (unveil(PATH_MAILZ_CONTENT, "x") == -1)
		goto null;
	if (unveil(PATH_SENDMAIL, "x") == -1)
		goto null;
	if (pledge("stdio rpath cpath wpath proc exec sendfd", NULL) == -1)
		err(1, "pledge");

	if (setup_letters(root, cur) == -1)
		goto null;

	if (read_letters(cur, view_all, null, &letters, &nletter) == -1)
		goto null;

	if (nletter == 0)
		puts("No mail.");
	else {
		for (i = 0; i < nletter; i++)
			letter_print(i + 1, &letters[i]);
		commands_run(letters, nletter, cur, null, tmpdir,
			     conf.address, &conf.ignore);
	}

	rv = 0;
	for (i = 0; i < nletter; i++)
		letter_free(&letters[i]);
	free(letters);
	null:
	close(null);
	tmpdir:
	rmdir(tmpdir);
	cur:
	close(cur);
	root:
	close(root);
	conf:
	mailz_conf_free(&conf);
	return rv;
}
