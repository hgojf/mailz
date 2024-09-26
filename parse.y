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

%{
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>

#include "conf.h"

extern FILE *yyin;
extern int yylex(void);
extern int yylex_destroy(void);
extern int yylineno;

void yyerror(const char *);
int yywrap(void);

static struct mailz_conf *conf;
static const char *filename;
%}

%union {
	struct {
		char *addr;
		char *name;
	} addr;
	struct {
		size_t argc;
		char **argv;
	} argv;
	long long number;
	char *string;
}

%token BADNUM OOM
%token ADDRESS CACHE IGNORE LINEWRAP NO REORDER RETAIN
%token<string> STRING
%token<number> NUMBER
%type<addr> addr
%type<argv> argument_list
%type<number> ignore_type
%%
grammar: /* empty */
	| grammar address '\n'
	| grammar cache '\n'
	| grammar ignore '\n'
	| grammar linewrap '\n'
	| grammar reorder '\n'
	| grammar '\n'
	;

address: ADDRESS addr {
		free(conf->address.addr);
		free(conf->address.name);

		conf->address.addr = $2.addr;
		conf->address.name = $2.name;
	}
	;

addr: STRING {
		$$.addr = $1;
		$$.name = NULL;
	}
	| STRING '<' STRING '>' {
		$$.name = $1;
		$$.addr = $3;
	}
	;

argument_list: STRING {
		$$.argv = reallocarray(NULL, 1, sizeof(*$$.argv));
		if ($$.argv == NULL) {
			warn(NULL);
			free($1);
			YYABORT;
		}

		$$.argc = 1;
		$$.argv[0] = $1;
	}
	| argument_list STRING {
		$$.argv = reallocarray($1.argv, $1.argc + 1, sizeof(*$1.argv));
		if ($$.argv == NULL) {
			warn(NULL);
			for (size_t i = 0; i < $1.argc; i++)
				free($1.argv[i]);
			free($1.argv);
			free($2);
			YYABORT;
		}

		$$.argc = $1.argc + 1;
		$$.argv[$1.argc] = $2;
	}
	;

cache: NO CACHE {
		conf->cache = 1;
	}
	| CACHE {
		conf->cache = 1;
	}

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
			yyerror("linewrap negative");
			YYERROR;
		}

		conf->linewrap = $2;
	}


reorder: REORDER argument_list {
		for (size_t i = 0; i < conf->reorder.argc; i++)
			free(conf->reorder.argv[i]);
		free(conf->reorder.argv);

		conf->reorder.argc = $2.argc;
		conf->reorder.argv = $2.argv;
	}
	;
%%
void
config_default(struct mailz_conf *c)
{
	memset(c, 0, sizeof(*c));
}

FILE *
config_file(void)
{
	const char *home, *mailrc;
	char path[PATH_MAX];
	int n;

	if ((mailrc = getenv("MAILZRC")) != NULL)
		return fopen(mailrc, "r");

	if ((home = getenv("HOME")) == NULL) {
		errno = ENOENT;
		return NULL;
	}

	n = snprintf(path, sizeof(path), "%s/.mailz.conf", home);
	if (n < 0)
		return NULL;
	if ((size_t)n >= sizeof(path)) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	return fopen(path, "r");
}

void
config_free(struct mailz_conf *c)
{
	free(c->address.addr);
	free(c->address.name);

	for (size_t i = 0; i < c->ignore.argc; i++)
		free(c->ignore.argv[i]);
	free(c->ignore.argv);

	for (size_t i = 0; i < c->reorder.argc; i++)
		free(c->reorder.argv[i]);
	free(c->reorder.argv);
}

int
config_init(struct mailz_conf *c, FILE *fp, const char *path)
{
	config_default(c);

	conf = c;
	filename = path;
	yyin = fp;

	if (yyparse() != 0) {
		config_free(c);
		yylex_destroy();
		return -1;
	}

	yylex_destroy();
	return 0;
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
