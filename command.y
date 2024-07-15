%{
#include <sys/mman.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "../address.h"
#include "../argv.h"
#include "../edit.h"
#include "../config.h"
#include "../letter.h"
#include "../maildir.h"
#include "../maildir-read-letter.h"
#include "../maildir-send.h"
#include "../pathnames.h"

struct interactive {
	const char *root;
	struct letter *letters;
	size_t nletters;
	int cur;

	size_t letter;

	int dev_null;
};

enum {
	CONFIG_NOTFOUND,
	CONFIG_ERRORED,
	CONFIG_FOUND,
};

static int config_location(char [static PATH_MAX]);
int letter_print(size_t, struct letter *);

static int argv_shm(struct argv *, struct argv_shm *);

static int letter_mark_read(int, struct letter *);
static int letter_mark_unread(int, struct letter *);

extern int yylex_destroy(void);
extern int yylex(void);
extern int yylineno;
extern FILE *yyin;

void yyerror(const char *);
int yywrap(void);

static struct config *config;
static const char *config_path;
static struct interactive *interactive;

%}

%token ADDRESS CACHE EDIT IGNORE MANUAL PRINT MORE NUMBER READ REORDER SAVE
%token SEND SET STRING THREAD UNIGNORE UNREAD VI

%union {
	struct address address;
	struct argv argv;
	enum edit_mode edit_mode;
	char *string;
	long long number;
}

%type<address> address
%type<argv> argument_list
%type<edit_mode> edit_mode
%type<number> message_number
%type<number> optional_message_number
%type<number> NUMBER
%type<string> STRING

%%
toplevel: command '\n' {
		if (interactive != NULL)
			return 0;
	}
	| toplevel command '\n'
	;

command: ignore
	| more
	| message_number {
		struct maildir_read_letter mdrl;

		mdrl = maildir_read_letter(interactive->root,
			interactive->letters[$1].path, 
			interactive->dev_null, stdout,
			config->ignore.type == IGNORE_RETAIN,
			&config->ignore.shm,
			&config->reorder.shm);
		if (mdrl.status != 0)
			warnc(mdrl.save_errno, "maildir_read_letter %d", mdrl.status);

		if (letter_mark_read(interactive->cur, &interactive->letters[$1]) == -1)
			YYERROR;
	}
	| print
	| read
	| reorder
	| save 
	| send
	| set
	| thread
	| unignore
	| unread
	;

more: MORE optional_message_number {
		FILE *fp;
		struct maildir_read_letter mdrl;
		pid_t pid;
		int p[2], status;

		if (pipe(p) == -1) {
			warn("pipe");
			YYERROR;
		}

		if ((fp = fdopen(p[1], "w")) == NULL) {
			warn("fdopen");
			(void) close(p[0]);
			(void) close(p[1]);
			YYERROR;
		}

		switch (pid = fork()) {
		case -1:
			warn("fork");
			fclose(fp);
			close(p[0]);
			YYERROR;
		case 0:
			if (dup2(p[0], STDIN_FILENO) == -1)
				err(1, "dup2");
			if (close(p[0]) == -1)
				err(1, "close");
			if (close(p[1]) == -1)
				err(1, "close");
			execl(PATH_LESS, "less", "-", NULL);
			err(1, "exec %s", PATH_LESS);
		default:
			break;
		}

		if (close(p[0]) == -1) {
			(void) kill(pid, SIGKILL);
			(void) waitpid(pid, NULL, 0);
			(void) fclose(fp);
			warn("close");
			YYERROR;
		}

		mdrl = maildir_read_letter(interactive->root,
			interactive->letters[$2].path, interactive->dev_null, fp,
			config->ignore.type == IGNORE_RETAIN,
			&config->ignore.shm,
			&config->reorder.shm);
		if (mdrl.status != 0 && mdrl.status != MAILDIR_READ_LETTER_PRINTF) {
			/* 
			 * maildir_read_letter will get a broken pipe if the pager
			 * exits without reading the entire file
			 */
			(void) kill(pid, SIGKILL);
			(void) waitpid(pid, NULL, 0);
			(void) fclose(fp);
			warnx("maildir_read_letter");
			YYERROR;
		}

		if (fclose(fp) == EOF) {
			(void) kill(pid, SIGKILL);
			(void) waitpid(pid, NULL, 0);
			warn("fclose");
			YYERROR;
		}

		if (waitpid(pid, &status, 0) == -1) {
			warn("waitpid");
			YYERROR;
		}

		if (WEXITSTATUS(status) != 0) {
			warnx("pager failed: exit status %d", WEXITSTATUS(status));
			YYERROR;
		}

		if (letter_mark_read(interactive->cur, &interactive->letters[$2]) == -1)
			YYERROR;
	}
	;

save: SAVE optional_message_number {
		char path[] = PATH_TMPDIR "save.XXXXXX";
		struct maildir_read_letter mdrl;
		int fd;
		FILE *fp;

		if ((fd = mkstemp(path)) == -1) {
			warn("mkstemp");
			YYERROR;
		}
		if ((fp = fdopen(fd, "w")) == NULL) {
			warn("fdopen");
			(void) close(fd);
			YYERROR;
		}

		mdrl = maildir_read_letter(interactive->root,
			interactive->letters[$2].path, interactive->dev_null, fp,
			config->ignore.type == IGNORE_RETAIN,
			&config->ignore.shm,
			&config->reorder.shm);

		if (mdrl.status != 0 && mdrl.status != MAILDIR_READ_LETTER_PIPE) {
			warnx("maildir_read_letter");
			(void) fclose(fp);
			YYERROR;
		}

		if (fclose(fp) == EOF) {
			warn("fclose");
			YYERROR;
		}

		printf("message saved to %s\n", path);
	}
	;

send: SEND STRING address {
		int err;
		if (interactive == NULL) {
			yyerror("no interactive");
			YYERROR;
		}

		if (config->address.addr == NULL) {
			warnx("must set an address");
			YYERROR;
		}

		err = maildir_send(config->edit_mode, config->address.addr,
				$2, $3.addr);

		free($3.addr);
		free($3.name);
		free($2);
		if (err == -1) {
			warnx("maildir_send");
			YYERROR;
		}
	}
	;

optional_message_number: message_number { $$ = $1; }
	| /* empty */ {
		if (interactive == NULL) {
			yyerror("not interactive");
			YYERROR;
		}
		$$ = interactive->letter;
	}
	;

message_number: NUMBER {
		if (interactive == NULL) {
			yyerror("not interactive");
			YYERROR;
		}
		if ($1 > interactive->nletters) {
			yyerror("message number was too large");
			YYERROR;
		}
		if ($1 == 0) {
			yyerror("message number was too small");
			YYERROR;
		}
		interactive->letter = $1 - 1;
		$$ = $1 - 1;
	}
	;

print: PRINT message_number {
		if (letter_print($2 + 1, &interactive->letters[$2]) == -1) {
			warnx("print_letter");
			YYERROR;
		}
	}
	| PRINT /* nothing */ {
		for (size_t i = 0; i < interactive->nletters; i++) {
			if (letter_print(i + 1, &interactive->letters[i]) == -1) {
				warnx("print_letter");
				YYERROR;
			}
		}
	}
	;

read: READ message_number {
		if (letter_mark_read(interactive->cur, &interactive->letters[$2]) == -1)
			YYERROR;
	}
	;

reorder: REORDER argument_list {
		struct argv_shm shm;

		if (argv_shm(&$2, &shm) == -1) {
			warn("shm_argv");
			YYERROR;
		}

		for (size_t i = 0; i < config->reorder.argv.argc; i++)
			free(config->reorder.argv.argv[i]);
		free(config->reorder.argv.argv);
		if (config->reorder.shm.fd != -1)
			(void) close(config->reorder.shm.fd);

		config->reorder.argv = $2;
		config->reorder.shm = shm;
	}
	| REORDER {
		for (size_t i = 0; i < config->reorder.argv.argc; i++)
			printf("%s, ", config->reorder.argv.argv[i]);
		printf("\n");
	}
	;

thread: THREAD message_number {
		struct letter *letter;
		const char *subject;
		size_t start;
		int re;

		letter = &interactive->letters[$2];

		/* cant find a thread without a subject */
		if (letter->subject == NULL) {
			if (letter_print($2 + 1, letter) == -1) {
				warnx("letter_print");
				YYERROR;
			}
			YYACCEPT;
		}

		if (!strncmp(letter->subject, "Re: ", strlen("Re: "))) {
			subject = letter->subject + strlen("Re: ");
			re = 1;
		}
		else {
			subject = letter->subject;
			re = 0;
		}

		/*
		 * if this is the first message in a thread, dont search mails
		 * before it
		 */
		if (!re) {
			start = $2 + 1;
			/* wont be printed in loop */
			if (letter_print($2 + 1, letter) == -1) {
				warnx("letter_print");
				YYERROR;
			}
		}
		else
			start = 0;

		for (size_t i = start; i < interactive->nletters; i++) {
			struct letter *l = &interactive->letters[i];

			if ((!strncmp(l->subject, "Re: ", strlen("Re: ")) &&
					!strcmp(l->subject + strlen("Re: "), subject))
					|| (re && !strcmp(l->subject, subject))) {
				if (letter_print(i + 1, l) == -1) {
					warnx("letter_print");
					YYERROR;
				}
			}
		}
	}
	;

ignore: IGNORE argument_list {
		struct argv_shm shm;

		if (argv_shm(&$2, &shm) == -1) {
			warn("argv_shm");
			YYERROR;
		}

		for (size_t i = 0; i < config->ignore.argv.argc; i++)
			free(config->ignore.argv.argv[i]);
		free(config->ignore.argv.argv);
		if (config->ignore.shm.fd != -1)
			(void) close(config->ignore.shm.fd);

		config->ignore.type = IGNORE_IGNORE;
		config->ignore.argv = $2;
		config->ignore.shm = shm;
	}
	| IGNORE {
		if (config->ignore.type != IGNORE_IGNORE) {
			printf("unignore is set\n");
		}
		else {
			for (size_t i = 0; i < config->ignore.argv.argc; i++) {
				printf("%s, ", config->ignore.argv.argv[i]);
			}
			printf("\n");
		}
	}
	;

unignore: UNIGNORE argument_list {
		struct argv_shm shm;
		if (argv_shm(&$2, &shm) == -1) {
			warn("argv_shm");
			YYERROR;
		}

		for (size_t i = 0; i < config->ignore.argv.argc; i++)
			free(config->ignore.argv.argv[i]);
		free(config->ignore.argv.argv);
		if (config->ignore.shm.fd != -1)
			(void) close(config->ignore.shm.fd);

		config->ignore.type = IGNORE_RETAIN;
		config->ignore.argv = $2;
		config->ignore.shm = shm;
	}
	| UNIGNORE {
		if (config->ignore.type != IGNORE_RETAIN) {
			printf("ignore is set\n");
		}
		else {
			for (size_t i = 0; i < config->ignore.argv.argc; i++) {
				printf("%s, ", config->ignore.argv.argv[i]);
			}
			printf("\n");
		}
	}
	;

unread: UNREAD optional_message_number {
		if (letter_mark_unread(interactive->cur, &interactive->letters[$2]) == -1)
			YYERROR;
	}
	;

argument_list: STRING {
		$$.argv = reallocarray(NULL, 1, sizeof(*$$.argv));
		if ($$.argv == NULL) {
			warn("reallocarray %zu", sizeof(*$$.argv));
			free($1);
			YYERROR;
		}
		$$.argv[0] = $1;
		$$.argc = 1;
	}
	| argument_list STRING {
		$$.argv = reallocarray($1.argv, $1.argc + 1, sizeof(*$1.argv));
		if ($$.argv == NULL) {
			for (size_t i = 0; i < $1.argc; i++)
				free($1.argv[i]);
			free($1.argv);
			free($2);
			warn("reallocarray %lld * %zu", $$.argc + 1, sizeof(*$1.argv));
			YYERROR;
		}
		$$.argc = $1.argc;
		$$.argv[$$.argc++] = $2;
	}
	;

set: SET EDIT edit_mode {
		config->edit_mode = $3;
	}
	| SET CACHE {
		config->cache = 1;
	}
	| SET ADDRESS address {
		if (config->address.addr != NULL) {
			free(config->address.addr);
			free(config->address.name);
		}
		config->address = $3;
	}
	;

edit_mode: MANUAL { $$ = EDIT_MODE_MANUAL; }
	| VI { $$ = EDIT_MODE_VI; }
	;

address: STRING '<' STRING '>' {
		$$.addr = $1;
		$$.name = $3;
	}
	| STRING {
		$$.addr = $1;
		$$.name = NULL;
	}
	;

%%

int
yywrap(void)
{
	return 1;
}

void
yyerror(const char *s)
{
	if (config_path != NULL) {
		fprintf(stderr, "%s: %s at line %d\n", config_path, s, yylineno);
	}
	else {
		fprintf(stderr, "%s\n", s);
	}
}

int
command(struct config *cfg, struct letter *letters, size_t nletters, 
	const char *root, int dev_null, int cur)
{
	struct interactive in;

	config = cfg;
	config_path = NULL;

	in.letters = letters;
	in.nletters = nletters;
	in.root = root;
	in.dev_null = dev_null;
	in.cur = cur;

	assert(nletters > 0);
	in.letter = 0;

	interactive = &in;

	yyin = stdin;

	for (size_t i = 0; i < nletters; i++)
		if (letter_print(i + 1, &letters[i]) == -1)
			return -1;

	while (!feof(stdin) && !ferror(stdin)) {
		printf("> ");
		yyparse();
		yylex_destroy();
	}

	if (ferror(stdin)) {
		warn(NULL);
		return -1;
	}

	return 0;
}

int
configure(struct config *out)
{
	char path[PATH_MAX];
	FILE *fp;

	memset(out, 0, sizeof(*out));
	out->ignore.shm.fd = -1;
	out->ignore.shm.sz = 0;
	out->reorder.shm.fd = -1;
	out->reorder.shm.sz = 0;

	switch (config_location(path)) {
	case 0:
		break;
	case -1:
		return 0;
	case -2:
		return -2;
	}

	if ((fp = fopen(path, "r")) == NULL) {
		if (errno == ENOENT)
			return 0;
		warn("%s", path);
		return -1;
	}

	config = out;
	config_path = path;
	yyin = fp;

	if (yyparse() != 0) {
		(void) fclose(fp);
		return -1;
	}

	if (fclose(fp) == EOF)
		return -1;

	yylex_destroy();
	return 0;
}

static int
config_location(char out[static PATH_MAX])
{
	char *home, *mailrc;
	int n;

	if ((mailrc = getenv("MAILZRC")) != NULL) {
		if (strlcpy(out, mailrc, PATH_MAX) >= PATH_MAX)
			return CONFIG_ERRORED;
		return CONFIG_FOUND;
	}

	if ((home = getenv("HOME")) == NULL)
		return CONFIG_NOTFOUND;
	n = snprintf(out, PATH_MAX, "%s/.mailzrc", home);
	if (n < 0 || n >= PATH_MAX)
		return CONFIG_ERRORED;

	return CONFIG_FOUND;
}

int
letter_print(size_t nth, struct letter *letter)
{
	struct tm *tm;
	const char *subject;
	char date[30];
	struct from from;

	from_extract(&letter->from, &from);

	if ((tm = localtime(&letter->date)) == NULL
				|| strftime(date, sizeof(date), "%a %b %d %H:%M", tm) == 0)
			return -1;
	subject = letter->subject == NULL ? "No Subject" : letter->subject;

	if (printf("%4zu %-20s %-32.*s %-30s\n", nth, date,
				from.al, from.addr, subject) < 0)
			return -1;
	return 0;
}

static int
argv_shm(struct argv *in, struct argv_shm *out)
{
	char path[] = PATH_TMPDIR "shm.XXXXXX";
	size_t len;
	void *p;
	int shm;

	if ((shm = shm_mkstemp(path)) == -1)
		return -1;

	len = 0;
	for (size_t i = 0; i < in->argc; i++)
		len += strlen(in->argv[i]) + 1;

	if (ftruncate(shm, len) == -1)
		goto shm;

	p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
	if (p == MAP_FAILED)
		goto shm;

	for (size_t i = 0, off = 0; i < in->argc; i++) {
		off += strlcpy(&p[off], in->argv[i], len - off) + 1;
	}

	if (munmap(p, len) == -1)
		goto shm;
	if (close(shm) == -1) {
		(void) unlink(path);
		return -1;
	}

	if ((shm = shm_open(path, O_RDONLY, 0400)) == -1) {
		(void) unlink(path);
		return -1;
	}
	if (shm_unlink(path) == -1) {
		(void) close(shm);
		return -1;
	}

	out->fd = shm;
	out->sz = len;

	return 0;

	shm:
	(void) close(shm);
	(void) unlink(path);
	return -1;
}

int
letter_mark_read(int root, struct letter *letter)
{
	const char *flags;
	char *new;
	int n;

	if ((flags = strstr(letter->path, ":2,")) != NULL) {
		if (strchr(flags, 'S') != NULL)
			return 0;
	}

	if (flags != NULL) {
		n = asprintf(&new, "%sS", letter->path);
	}
	else
		n = asprintf(&new, "%s:2,S", letter->path);
	if (n == -1)
		return -1;

	if (renameat(root, letter->path, root, new) == -1) {
		warn("renameat %s", letter->path);
		free(new);
		return -1;
	}

	free(letter->path);
	letter->path = new;
	return 0;
}

static int
letter_mark_unread(int root, struct letter *letter)
{
	const char *f, *flags;
	size_t idx;
	char new[NAME_MAX];

	if ((flags = strstr(letter->path, ":2,")) == NULL 
			|| (f = strchr(flags, 'S')) == NULL)
		return 0;

	/* path is returned by readdir, so it will never be longer than NAME_MAX */
	(void) strlcpy(new, letter->path, sizeof(new));

	idx = f - letter->path;
	memmove(&new[idx], &new[idx+1], strlen(&new[idx]) + 1);

	if (renameat(root, letter->path, root, new) == -1) {
		warn("renameat %s", letter->path);
		return -1;
	}

	(void) strlcpy(letter->path, new, strlen(letter->path) + 1);

	return 0;
}
