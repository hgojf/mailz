#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "conf.h"
#include "letter.h"
#include "commands.h"
#include "pathnames.h"
#include "read-letter.h"
#include "send.h"

static int more(size_t, int, struct mailbox *, struct mailz_conf *);
static int print(size_t, int, struct mailbox *, struct mailz_conf *);
static int read_cmd(size_t, int, struct mailbox *, struct mailz_conf *);
static int reply(size_t, int, struct mailbox *, struct mailz_conf *);
static int send(char *, int, struct mailbox *, struct mailz_conf *);
static int save(size_t, int, struct mailbox *, struct mailz_conf *);
static int thread(size_t, int, struct mailbox *, struct mailz_conf *);
static int unread(size_t, int, struct mailbox *, struct mailz_conf *);

static int prompt_letter(const char *);
static int sendmail_interactive(const struct sendmail_subject *subject,
	const struct sendmail_from *from, 
	const struct sendmail_from *to, 
	const struct sendmail_header *headers, size_t nh,
	char *path);

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

static struct command { // NOLINT(clang-analyzer-optin.performance.Padding)
	#define CMD_NOALIAS '\0'
	char alias;
	const char *ident;
	#define CMD_MESSAGE 0
	#define CMD_OPTMESSAGE 1
	#define CMD_FREEFORM 2
	int type;
	union {
		int (*msg) (size_t, int, struct mailbox *, struct mailz_conf *);
		int (*freeform) (char *, int, struct mailbox *, struct mailz_conf *);
	} fn;
} commands[] = {
	{ CMD_NOALIAS, "more", CMD_MESSAGE, .fn.msg = more },
	{ 'p', "print", CMD_OPTMESSAGE, .fn.msg = print },
	{ 'r', "read", CMD_MESSAGE, .fn.msg = read_cmd },
	{ CMD_NOALIAS, "reply", CMD_MESSAGE, .fn.msg = reply },
	{ 's', "save", CMD_MESSAGE, .fn.msg = save },
	{ CMD_NOALIAS, "send", CMD_FREEFORM, .fn.freeform = send },
	{ 't', "thread", CMD_MESSAGE, .fn.msg = thread },
	{ 'x', "unread", CMD_MESSAGE, .fn.msg = unread },
};

static int
cat_letter(size_t idx, int cur, struct mailbox *mbox, struct mailz_conf *conf)
{
	if (read_letter_quick(cur, mbox->letters[idx].path, &conf->ignore, 
			&conf->reorder, conf->linewrap, stdout) == -1)
		return -1;

	return read_cmd(idx, cur, mbox, conf);
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

	return strcmp(one, n2->ident);
}

int
commands_run(int cur, struct mailbox *mbox, struct mailz_conf *conf)
{
	char *line;
	size_t sidx, n;

	line = NULL;
	n = 0;

	sidx = 0;
	for (;;) {
		static const struct command *cmd;
		char *args, *arg0;
		const char *errstr;
		ssize_t len;
		size_t idx;
		int rv;

		fputs("> ", stdout);

		if ((len = getline(&line, &n, stdin)) == -1)
			break;

		if (line[len - 1] == '\n')
			line[len - 1] = '\0';

		if (*line == '\0')
			continue;

		args = line;

		if (isdigit((unsigned char)*args)) {
			idx = strtonum(args, 0, mbox->nletter, &errstr);
			if (errstr != NULL) {
				warnx("message number was %s", errstr);
				continue;
			}
			idx -= 1;

			sidx = idx;

			if (cat_letter(idx, cur, mbox, conf) == -1) {
				warn("failed to print letter");
				continue;
			}

			continue;
		}

		if ((arg0 = strsep(&args, " \t")) == NULL)
			continue;

		cmd = bsearch(arg0, commands, nitems(commands), sizeof(*commands),
			command_cmp);

		if (cmd == NULL) {
			warnx("unknown command '%s'", arg0);
			continue;
		}

		switch (cmd->type) {
		case CMD_FREEFORM:
			rv = cmd->fn.freeform(args, cur, mbox, conf);
			break;
		case CMD_MESSAGE:
		case CMD_OPTMESSAGE:
			if (args == NULL) {
				if (cmd->type == CMD_OPTMESSAGE)
					idx = mbox->nletter;
				else
					idx = sidx;
			}
			else {
				idx = strtonum(args, 1, mbox->nletter, &errstr);
				if (errstr != NULL) {
					warnx("message number was %s", errstr);
					continue;
				}
				idx -= 1;
	
				sidx = idx;
			}
			rv = cmd->fn.msg(idx, cur, mbox, conf);
			break;
		default:
			/* NOTREACHED */
			abort();
		}

		if (rv == -1) {
			warn("command '%s' failed", cmd->ident);
			continue;
		}
	}

	fputc('\n', stdout);

	free(line);
	return 0;
}

static int
more(size_t idx, int cur, struct mailbox *mbox, struct mailz_conf *conf)
{
	FILE *o;
	pid_t pid;
	int p[2], status;

	if (pipe2(p, O_CLOEXEC) == -1)
		return -1;
	if ((o = fdopen(p[1], "w")) == NULL) {
		close(p[0]);
		close(p[1]);
		return -1;
	}

	switch (pid =fork()) {
	case -1:
		fclose(o);
		close(p[0]);
		return -1;
	case 0:
		if (dup2(p[0], STDIN_FILENO) == -1)
			err(1, "dup2");
		execl(PATH_LESS, "less", NULL);
		err(1, "%s", PATH_LESS);
	default:
		break;
	}

	close(p[0]);

	if (read_letter_quick(cur, mbox->letters[idx].path, &conf->ignore, 
			&conf->reorder, conf->linewrap, o) == -1)
		goto pid;

	if (fflush(o) == EOF && ferror(o) && errno != EPIPE)
		goto pid;

	fclose(o);
	if (waitpid(pid, &status, 0) == -1 || WEXITSTATUS(status) != 0)
		return -1;

	return read_cmd(idx, cur, mbox, conf);

	pid:
	(void)fclose(o);
	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, NULL, 0);
	return -1;
}

static int
print(size_t idx, int cur, struct mailbox *mbox, struct mailz_conf *conf)
{
	if (idx == mbox->nletter) {
		for (size_t i = 0; i < mbox->nletter; i++)
			if (letter_print(i + 1, &mbox->letters[i]) == -1)
				return -1;
		return 0;
	}

	return letter_print(idx + 1, &mbox->letters[idx]);
}

static int
read_cmd(size_t idx, int cur, struct mailbox *mbox, struct mailz_conf *conf)
{
	char *flags, *new;
	struct letter *letter;
	int n;

	letter = &mbox->letters[idx];

	/* already marked read */
	if ((flags = strstr(letter->path, ":2,")) != NULL
			&& strchr(flags + 3, 'S') != NULL)
		return 0;

	if (flags == NULL)
		n = asprintf(&new, "%s:2,S", letter->path);
	else
		n = asprintf(&new, "%sS", letter->path);

	if (n == -1) /* errno == ENOMEM */
		return -1;

	if (renameat(cur, letter->path, cur, new) == -1) {
		free(new);
		return -1;
	}

	free(letter->path);
	letter->path = new;

	return 0;
}

static int
reply(size_t idx, int cur, struct mailbox *mbox, struct mailz_conf *conf)
{
	const struct letter *letter;
	struct ignore ignore;
	struct read_letter rl;
	struct sendmail_from from, to;
	struct sendmail_header hv[1];
	struct sendmail_subject subject;
	char date[33];
	char path[] = PATH_TMPDIR "reply.XXXXXX";
	struct tm *tm;
	FILE *fp;
	int fd, hc, lastnl, rv;

	rv = -1;

	if (conf->address.addr == NULL) {
		errno = EINVAL;
		return -1;
	}

	letter = &mbox->letters[idx];

	from.addr = conf->address.addr;
	from.name = conf->address.name;

	subject.reply = 1;
	subject.s = letter->subject;
	if (letter->subject != NULL && !strncmp(letter->subject, "Re: ", 4))
		subject.s += 4;

	to.addr = letter->from.addr;
	to.name = letter->from.name;

	hc = 0;
	if (letter->message_id != NULL) {
		hv[hc].key = "Reply-To";
		hv[hc].val = letter->message_id;
		hc++;
	}

	if ((fd = mkostemp(path, O_CLOEXEC)) == -1)
		return -1;
	if ((fp = fdopen(fd, "r+")) == NULL) {
		close(fd);
		goto unlink;
	}

	if (sendmail_setup(&subject, &from, &to, hv, hc, fp) == -1)
		goto fp;

	memset(&ignore, 0, sizeof(ignore));
	ignore.type = IGNORE_ALL;
	if (read_letter(cur, letter->path, &ignore, &conf->reorder, 
			conf->linewrap, &rl) == -1)
		goto fp;

	if ((tm = localtime(&letter->date)) == NULL)
		goto fp;
	if (strftime(date, sizeof(date), "%d %b %Y %H:%M:%S", tm) == 0)
		goto fp;

	if (fprintf(fp, "On %s ", date) < 0)
		goto fp;

	if (letter->from.name != NULL) {
		if (fprintf(fp, "%s <%s> ", letter->from.name, letter->from.addr) < 0)
			goto fp;
	}
	else {
		if (fprintf(fp, "%s ", letter->from.addr) < 0)
			goto fp;
	}

	if (fputs("wrote:\n", fp) == EOF)
		goto fp;

	lastnl = 1;
	for (;;) {
		char buf[4];
		int n;

		if ((n = read_letter_getc(&rl, buf)) == -1)
			goto rl;
		if (n == 0)
			break;

		if (lastnl) {
			if (fputs("\n> ", fp) == EOF)
				goto rl;
			lastnl = 0;
		}

		if (n == 1 && buf[0] == '\n') {
			lastnl = 1;
			continue;
		}

		if (fwrite(buf, n, 1, fp) != 1)
			goto rl;
	}

	if (fflush(fp) == EOF)
		goto rl;

	switch (prompt_letter(path)) {
	case -1:
		goto rl;
	case 0:
		break;
	case 1:
		goto good;
	}

	if (sendmail_send(fp) == -1)
		goto rl;

	good:
	rv = 0;
	rl:
	read_letter_close(&rl);
	fp:
	fclose(fp);
	unlink:
	unlink(path);
	return rv;
}

static int
save(size_t idx, int cur, struct mailbox *mbox, struct mailz_conf *conf)
{
	char path[] = PATH_TMPDIR "save.XXXXXX";
	FILE *fp;
	int fd;

	if ((fd = mkostemp(path, O_CLOEXEC)) == -1)
		return -1;
	if ((fp = fdopen(fd, "w")) == NULL) {
		close(fd);
		goto unlink;
	}

	if (read_letter_quick(cur, mbox->letters[idx].path, &conf->ignore, 
			&conf->reorder, conf->linewrap, fp) == -1)
		goto fp;
	if (fflush(fp) == EOF)
		goto fp;

	printf("letter saved to %s\n", path);
	fclose(fp);

	return 0;

	fp:
	fclose(fp);
	unlink:
	unlink(path);
	return -1;
}

static int
send(char *args, int cur, struct mailbox *mbox, struct mailz_conf *conf)
{
	struct sendmail_from from, to;
	struct sendmail_subject subject;
	char path[] = PATH_TMPDIR "send.XXXXXX";

	if (conf->address.addr == NULL) {
		errno = EINVAL;
		return -1;
	}

	if ((to.addr = strsep(&args, " \t")) == NULL) {
		errno = EINVAL;
		return -1;
	}
	to.name = NULL;

	subject.s = args;
	subject.reply = 0;

	from.addr = conf->address.addr;
	from.name = conf->address.name;

	if (sendmail_interactive(&subject, &from, &to, NULL, 0, path) == -1)
		return -1;
	return 0;
}

static int
thread(size_t idx, int cur, struct mailbox *mbox, struct mailz_conf *conf)
{
	struct letter *letter = &mbox->letters[idx];
	const char *subject;
	size_t start;
	int re;

	if ((subject = letter->subject) == NULL)
		return letter_print(idx + 1, letter);

	/* this logic behaves wrong in some cases, but mostly works */

	if (strncmp(subject, "Re: ", 4) == 0) {
		re = 1;
		subject += 4;
		start = 0;
	}
	else {
		re = 0;
		start = idx + 1;
		if (letter_print(idx + 1, letter) == -1)
			return -1;
	}

	for (size_t i = start; i < mbox->nletter; i++) {
		const char *ls = mbox->letters[i].subject;

		if (ls == NULL)
			continue;

		if ((strncmp(ls, "Re: ", 4) == 0 && strcmp(ls + 4, subject) == 0)
				|| (re && strcmp(ls, subject) == 0)) {
			if (letter_print(i + 1, &mbox->letters[i]) == -1)
				return -1;
		}
	}

	return 0;
}

static int
unread(size_t idx, int cur, struct mailbox *mbox, struct mailz_conf *conf)
{
	struct letter *letter;
	char *flags, *new;
	size_t flag_start, wp;

	letter = &mbox->letters[idx];

	/* not marked read */
	if ((flags = strstr(letter->path, ":2,")) == NULL
			|| strchr(flags + 3, 'S') == NULL)
		return 0;

	if ((new = strdup(letter->path)) == NULL)
		return -1;

	flag_start = (flags - letter->path) + 3;

	wp = flag_start;
	for (size_t i = flag_start; letter->path[i] != '\0'; i++) {
		if (letter->path[i] != 'S')
			new[wp++] = letter->path[i];
	}
	new[wp] = '\0';

	if (renameat(cur, letter->path, cur, new) == -1) {
		free(new);
		return -1;
	}

	free(letter->path);
	letter->path = new;

	return 0;
}

static int
sendmail_interactive(const struct sendmail_subject *subject,
	const struct sendmail_from *from, 
	const struct sendmail_from *to, 
	const struct sendmail_header *headers, size_t nh,
	char *path)
{
	FILE *fp;
	int fd, rv;

	rv = -1;

	if ((fd = mkstemp(path)) == -1)
		return -1;
	if ((fp = fdopen(fd, "r+")) == NULL) {
		close(fd);
		goto unlink;
	}

	if (sendmail_setup(subject, from, to, headers, nh, fp) == -1)
		goto fp;

	switch (prompt_letter(path)) {
	case -1:
		goto fp;
	case 0:
		break;
	case 1:
		goto good;
	}

	if (sendmail_send(fp) == -1)
		goto fp;

	good:
	rv = 0;
	fp:
	fclose(fp);
	unlink:
	unlink(path);
	return rv;
}

static int
prompt_letter(const char *path)
{
	int c;

	printf("Letter located at %s\n", path);
	fputs("Press enter after editing or q to cancel: ", stdout);

	switch (fgetc(stdin)) {
	case 'q':
		if (fgetc(stdin) != '\n') {
			while ((c = fgetc(stdin)) != '\n' && c != EOF) 
				;
			errno = EINVAL;
			return -1;
		}
		return 1;
	case '\n':
		return 0;
	default:
		while ((c = fgetc(stdin)) != '\n' && c != EOF) 
			;
		errno = EINVAL;
		return -1;
		break;
	}

	return 0;
}
