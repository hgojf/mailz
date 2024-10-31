#include <sys/wait.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "content-proc.h"
#include "maildir.h"
#include "pathnames.h"

struct command_args {
	const char *addr;
	const char *tmpdir;
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

static void commands_run(struct letter *, size_t, int, int, const char *);
static int commands_token(FILE *, char *, size_t, int *);
static const struct command *commands_search(const char *);
static int command_more(struct letter *, struct command_args *);
static int command_read(struct letter *, struct command_args *);
static int command_reply(struct letter *, struct command_args *);
static int command_save(struct letter *, struct command_args *);
static int command_thread(struct letter *, struct command_args *);
static int command_unread(struct letter *, struct command_args *);
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
	{ "save",	's',		command_save },
	{ "thread",	't',		command_thread },
	{ "unread",	'x',		command_unread },
};

static void
commands_run(struct letter *letters, size_t nletter, int cur,
	     int null, const char *tmpdir)
{
	struct command_args args;
	struct letter *letter;

	args.addr = NULL;
	args.addr = "jake";
	args.letters = letters;
	args.nletter = nletter;
	args.cur = cur;
	args.null = null;
	args.tmpdir = tmpdir;

	letter = NULL;
	for (;;) {
		const struct command *cmd;
		char buf[300];
		int ch, gotnl;

		printf("> ");

		gotnl = 0;
		if (commands_token(stdin, buf, sizeof(buf), &gotnl) == -1)
			goto bad;

		if (strlen(buf) == 0 && gotnl)
			break;

		if ((cmd = commands_search(buf)) == NULL)
			goto bad;

		if (gotnl) {
			if (letter == NULL)
				goto bad;
			if (cmd->fn(letter, &args) == -1)
				goto bad;
		}

		while (!gotnl) {
			const char *errstr;
			size_t idx;

			if (commands_token(stdin, buf, sizeof(buf), &gotnl) == -1)
				goto bad;

			idx = strtonum(buf, 1, nletter, &errstr);
			if (errstr != NULL)
				goto bad;

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

		if (ch == ' ' || ch == '\t')
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

	if (content_proc_ignore(&pr, "content-transfer-encoding", CNT_IGNORE_RETAIN) == -1)
		goto pr;
	if (content_proc_ignore(&pr, "date", CNT_IGNORE_RETAIN) == -1)
		goto pr;
	if (content_proc_ignore(&pr, "subject", CNT_IGNORE_RETAIN) == -1)
		goto pr;
	if (content_proc_ignore(&pr, "from", CNT_IGNORE_RETAIN) == -1)
		goto pr;
	if (content_proc_ignore(&pr, "to", CNT_IGNORE_RETAIN) == -1)
		goto pr;
	if (content_proc_ignore(&pr, "cc", CNT_IGNORE_RETAIN) == -1)
		goto pr;
	if (content_proc_ignore(&pr, "list-id", CNT_IGNORE_RETAIN) == -1)
		goto pr;

	if ((fd = openat(args->cur, letter->path,
			 O_RDONLY | O_CLOEXEC)) == -1)
		goto pr;
	if (content_letter_init(&pr, &lr, fd, 0) == -1)
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
			err(1, "dup2");
		execl(PATH_LESS, "less", NULL);
		err(1, "%s", PATH_LESS);
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

/*
 * XXX: this and command_unread are effectively the same,
 * should probably be merged into command_read1(letter, args, read)
 */
static int
command_read(struct letter *letter, struct command_args *args)
{
	const char *bufp;
	char buf[NAME_MAX], *new;

	if ((bufp = maildir_set_flag(letter->path, 'S',
				     buf, sizeof(buf))) == NULL)
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
command_reply(struct letter *letter, struct command_args *args)
{
	struct content_proc pr;
	struct content_letter lr;
	struct content_reply rpl;
	struct content_reply_summary sm;
	struct tm tm;
	char date[39], path[PATH_MAX];
	FILE *fp;
	pid_t pid;
	int ch, fd, lfd, n, putref, rpldone, rv, status;

	rv = -1;

	if (content_proc_init(&pr, PATH_MAILZ_CONTENT, args->null) == -1)
		return -1;

	if ((lfd = openat(args->cur, letter->path, O_RDONLY | O_CLOEXEC)) == -1)
		goto pr;
	if (content_reply_init(&pr, &rpl, &sm, lfd) == -1)
		goto pr;

	rpldone = 0;

	n = snprintf(path, sizeof(path), "%s/reply.XXXXXX", args->tmpdir);
	if (n < 0 || (size_t)n >= sizeof(path))
		goto rpl;

	if ((fd = mkstemp(path)) == -1)
		goto rpl;
	if ((fp = fdopen(fd, "w")) == NULL) {
		close(fd);
		goto rpl;
	}

	if (args->addr == NULL)
		goto fp;
	if (fprintf(fp, "From: %s\n", args->addr) < 0)
		goto fp;

	if (strlen(sm.message_id) != 0) {
		if (fprintf(fp, "In-Reply-To: <%s>\n", sm.message_id) < 0)
			goto fp;
	}

	if (strlen(sm.reply_to.addr) != 0) {
		if (strlen(sm.reply_to.name) != 0) {
			if (fprintf(fp, "To: %s <%s>\n",
				    sm.reply_to.name, sm.reply_to.addr) < 0)
				goto fp;
		}
		else
			if (fprintf(fp, "To: %s\n", sm.reply_to.addr) < 0)
				goto fp;
	}
	else {
		if (strlen(sm.name) != 0) {
			if (fprintf(fp, "To: %s <%s>\n", sm.name,
				    letter->from) < 0)
				goto fp;
		}
		else
			if (fprintf(fp, "To: %s\n", letter->from) < 0)
				goto fp;
	}

	if (letter->subject != NULL) {
		const char *sub;

		sub = letter->subject;
		if (!strncmp(sub, "Re: ", 4))
			sub += 4;

		if (fprintf(fp, "Subject: Re: %s\n", sub) < 0)
			goto fp;
	}

	putref = 0;
	for (;;) {
		struct content_reference ref;

		if ((n = content_reply_reference(&rpl, &ref)) == -1)
			goto fp;
		if (n == 0)
			break;

		if (!putref) {
			if (fprintf(fp, "References:") < 0)
				goto fp;
			putref = 1;
		}
		if (fprintf(fp, " <%s>", ref.id) < 0)
			goto fp;
	}

	if (!putref && strlen(sm.in_reply_to) != 0) {
		if (fprintf(fp, "References: <%s>",
			    sm.in_reply_to) < 0)
			goto fp;
		putref = 1;
	}

	if (strlen(sm.message_id) != 0) {
		if (!putref) {
			if (fprintf(fp, "References:") < 0)
				goto fp;
			putref = 1;
		}

		if (fprintf(fp, " <%s>", sm.message_id) < 0)
			goto fp;
	}

	if (putref)
		if (fprintf(fp, "\n") < 0)
			goto fp;

	if (fprintf(fp, "\n") < 0)
		goto fp;

	content_reply_close(&rpl);
	rpldone = 1;

	if (localtime_r(&letter->date, &tm) == NULL)
		goto fp;
	if (strftime(date, sizeof(date), "%a, %b %d, %Y at %H:%M:%S %p %z", &tm) == 0)
		goto fp;

	if (strlen(sm.name) != 0) {
		if (fprintf(fp, "On %s, %s <%s> wrote:\n",
			    date, sm.name, letter->from) < 0)
			goto fp;
	}
	else {
		if (fprintf(fp, "On %s, %s wrote:\n",
			    date, letter->from) < 0)
			goto fp;
	}

	if ((lfd = openat(args->cur, letter->path,
			  O_RDONLY | O_CLOEXEC)) == -1)
		goto fp;
	if (content_letter_init(&pr, &lr, lfd, CNT_LR_NOHDR) == -1)
		goto fp;

	if (fprintf(fp, "> ") < 0)
		goto fp;
	for (;;) {
		char buf[4];
		int n;

		if ((n = content_letter_getc(&lr, buf)) == -1)
			goto lr;
		if (n == 0)
			break;

		if (fwrite(buf, n, 1, fp) != 1)
			goto lr;
		if (n == 1 && buf[0] == '\n')
			if (fprintf(fp, "> ") < 0)
				goto lr;
	}

	if (fflush(fp) == EOF)
		goto lr;

	if (printf("message located at %s\n"
		   "press enter to send or q to cancel: ", path) < 0)
		goto lr;

	if ((ch = fgetc(stdin)) == EOF)
		goto lr;

	if (ch != '\n') {
		int c, n;

		n = 0;
		while ((c = fgetc(stdin)) == EOF && c != '\n')
			n++;
		if (ch == 'q' && n == 0)
			rv = 0;
		goto lr;
	}

	switch (pid = fork()) {
	case -1:
		goto lr;
	case 0:
		if (lseek(fileno(fp), 0, SEEK_SET) == -1)
			err(1, "lseek");
		if (dup2(fileno(fp), STDIN_FILENO) == -1)
			err(1, "dup2");
		execl(PATH_SENDMAIL, "sendmail", "-t", NULL);
		err(1, "%s", PATH_SENDMAIL);
	default:
		break;
	}

	if (waitpid(pid, &status, 0) == -1)
		goto lr;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		goto lr;

	rv = 0;
	lr:
	content_letter_close(&lr);
	fp:
	fclose(fp);
	unlink(path);
	rpl:
	if (!rpldone)
		content_reply_close(&rpl);
	pr:
	content_proc_kill(&pr);
	return rv;
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

	if ((lfd = openat(args->cur, letter->path,
			  O_RDONLY | O_CLOEXEC)) == -1)
		goto pr;
	if (content_letter_init(&pr, &lr, lfd, 0) == -1)
		goto pr;

	n = snprintf(path, sizeof(path), "%s/save.XXXXXX", args->tmpdir);
	if (n < 0 || (size_t)n >= sizeof(path))
		goto lr;

	if ((fd = mkstemp(path)) == -1)
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
	const char *bufp;
	char buf[NAME_MAX], *new;

	if ((bufp = maildir_unset_flag(letter->path, 'S',
				     buf, sizeof(buf))) == NULL)
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
	if (fcntl(curfd, F_SETFD, 1) == -1) {
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

	if ((root = open(argv[0], O_RDONLY | O_DIRECTORY | O_CLOEXEC)) == -1)
		return 1;
	if ((cur = openat(root, "cur", O_RDONLY | O_DIRECTORY | O_CLOEXEC)) == -1)
		goto root;

	if (mkdtemp(tmpdir) == NULL)
		goto cur;

	if ((null = open(PATH_DEV_NULL, O_RDONLY | O_CLOEXEC)) == -1)
		goto tmpdir;

	if (unveil(argv[0], "rc") == -1)
		goto null;
	if (unveil(PATH_LESS, "x") == -1)
		goto null;
	if (unveil(PATH_MAILZ_CONTENT, "x") == -1)
		goto null;
	if (unveil(PATH_SENDMAIL, "x") == -1)
		goto null;
	if (unveil(tmpdir, "rwc") == -1)
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
		commands_run(letters, nletter, cur, null, tmpdir);
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
	return rv;
}
