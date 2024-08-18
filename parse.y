%{
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conf.h"

extern FILE *yyin;
extern int yylex(void);
extern int yylex_destroy(void);
extern int yylineno;

void yyerror(const char *);
int yywrap(void);

static const char *filename;
static struct mailz_conf *conf;
%}

%union {
	struct {
		size_t argc;
		char **argv;
	} argv;
	char *string;
	int val;
	long long number;
}

%token ADDRESS BADNUM CACHE IGNORE LINEWRAP OOM NO REORDER RETAIN
%token<number> NUMBER
%token<string> STRING

%type<argv> argument_list

%type<val> ignore_type
%%
grammar: line '\n'
	| grammar line '\n'
	;

line: address
	| cache
	| ignore
	| linewrap
	| reorder
	| /* empty */
	;

address: ADDRESS {
		free(conf->address.addr);
		free(conf->address.name);
	} addr
	;

addr: STRING {
		conf->address.addr = $1;
		conf->address.name = NULL;
	}
	| STRING '<' STRING '>' {
		conf->address.addr = $3;
		conf->address.name = $1;
	}
	;

argument_list: STRING {
		$$.argv = reallocarray(NULL, 1, sizeof(*$$.argv));
		if ($$.argv == NULL) {
			warn(NULL);
			free($1);
			YYERROR;
		}
		$$.argv[0] = $1;
		$$.argc = 1;
	}
	| argument_list STRING {
		$$.argv = reallocarray($1.argv, $1.argc + 1, sizeof(*$$.argv));
		if ($$.argv == NULL) {
			warn(NULL);
			for (size_t i = 0; i < $1.argc; i++)
				free($1.argv[i]);
			free($1.argv);
			free($2);
			YYERROR;
		}

		$$.argv[$1.argc] = $2;
		$$.argc = $1.argc + 1;
	}
	;

cache: NO CACHE {
		conf->cache = 0;
	}
	| CACHE {
		conf->cache = 1;
	}
	;

ignore_type: IGNORE { $$ = IGNORE_IGNORE; }
	| RETAIN { $$ = IGNORE_RETAIN; }
	;

ignore: ignore_type argument_list {
		for (size_t i = 0; i < conf->ignore.argc; i++)
			free(conf->ignore.argv[i]);
		free(conf->ignore.argv);

		conf->ignore.type = $1;
		conf->ignore.argc = $2.argc;
		conf->ignore.argv = $2.argv;
	}
	;

linewrap: LINEWRAP NUMBER {
		if ($2 < 0) {
			yyerror("linewrap cannot be negative");
			YYERROR;
		}
		conf->linewrap = $2;
	}
	;

reorder: REORDER argument_list {
		for (size_t i = 0; i < conf->reorder.argc; i++)
			free(conf->reorder.argv[i]);
		free(conf->reorder.argv);

		conf->reorder.argc = $2.argc;
		conf->reorder.argv = $2.argv;
	}
	;
%%
int
configure(struct mailz_conf *out)
{
	const char *mailrc;
	char path[PATH_MAX];
	FILE *fp;

	memset(out, 0, sizeof(*out));

	if ((mailrc = getenv("MAILZRC")) == NULL) {
		const char *home;
		int n;

		if ((home = getenv("HOME")) == NULL)
			return 0;

		n = snprintf(path, sizeof(path), "%s/.mailz.conf", home);
		if (n < 0) {
			warn("snprintf");
			return -1;
		}
		if ((size_t)n >= sizeof(path)) {
			warnc(ENAMETOOLONG, "%s/.mailzrc", home);
			return -1;
		}

		mailrc = path;
	}

	if ((fp = fopen(path, "r")) == NULL) {
		if (errno == ENOENT)
			return 0;
		warn("%s", path);
		return -1;
	}

	conf = out;
	filename = mailrc;
	yyin = fp;

	if (yyparse() != 0) {
		yylex_destroy();
		goto config;
	}

	fclose(fp);
	yylex_destroy();
	return 0;

	config:
	config_free(out);
	fclose(fp);
	return -1;
}

void
config_free(struct mailz_conf *c)
{
	for (size_t i = 0; i < c->ignore.argc; i++)
		free(c->ignore.argv[i]);
	free(c->ignore.argv);

	for (size_t i = 0; i < c->reorder.argc; i++)
		free(c->reorder.argv[i]);
	free(c->reorder.argv);

	free(c->address.addr);
	free(c->address.name);
}

void
yyerror(const char *s)
{
	fprintf(stderr, "%s: %s on line %d\n", filename, s, yylineno);
}

int
yywrap(void)
{
	return 1;
}
