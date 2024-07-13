%{
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "../config.h"
#include "../letter.h"
#include "../maildir.h"
#include "../maildir-read-letter.h"
#include "../pathnames.h"

struct interactive {
	const char *root;
	struct letter *letters;
	size_t nletters;

	size_t letter;

	int dev_null;
};

enum {
	CONFIG_NOTFOUND,
	CONFIG_ERRORED,
	CONFIG_FOUND,
};

static int config_location(char [static PATH_MAX]);

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

%token ADDRESS CACHE EDIT IGNORE MANUAL MORE NUMBER REORDER SAVE SET STRING UNIGNORE VI

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
%type<number> NUMBER
%type<string> STRING

%%
toplevel: command '\n'
	| toplevel command '\n'
	;

command: ignore
	| more
	| message_number {
		struct maildir_read_letter mdrl;

		mdrl = maildir_read_letter(interactive->root,
			interactive->letters[$1].path, 
			interactive->dev_null, stdout);
		if (mdrl.status != 0)
			warnc(mdrl.save_errno, "maildir_read_letter %d", mdrl.status);
	}
	| reorder
	| save 
	| set
	| unignore
	;

more: MORE message_number {
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
			interactive->letters[$2].path, interactive->dev_null, fp);
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
	}
	;

save: SAVE message_number {
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
			interactive->letters[$2].path, interactive->dev_null, fp);

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
		$$ = $1 - 1;
	}
	;

reorder: REORDER argument_list {
		for (size_t i = 0; i < config->reorder.argc; i++)
			free(config->reorder.argv[i]);
		free(config->reorder.argv);

		config->reorder = $2;
	}
	| REORDER {
		for (size_t i = 0; i < config->reorder.argc; i++)
			printf("%s, ", config->reorder.argv[i]);
		printf("\n");
	}
	;

ignore: IGNORE argument_list {
		for (size_t i = 0; i < config->ignore.argv.argc; i++)
			free(config->ignore.argv.argv[i]);
		free(config->ignore.argv.argv);

		config->ignore.type = IGNORE_IGNORE;
		config->ignore.argv = $2;
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

unignore: UNIGNORE argument_list {
		for (size_t i = 0; i < config->ignore.argv.argc; i++)
			free(config->ignore.argv.argv[i]);
		free(config->ignore.argv.argv);

		config->ignore.type = IGNORE_RETAIN;
		config->ignore.argv = $2;
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
			warn("reallocarray %zu * %zu", $$.argc + 1, sizeof(*$1.argv));
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
	const char *root, int dev_null)
{
	struct interactive in;

	config = cfg;
	config_path = NULL;

	in.letters = letters;
	in.nletters = nletters;
	in.root = root;
	in.dev_null = dev_null;
	interactive = &in;

	yyin = stdin;

	while (!feof(stdin) && !ferror(stdin)) {
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
