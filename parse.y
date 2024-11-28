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
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "conf.h"

extern FILE *yyin;
extern int yylineno;
extern int yylex(void);

static void argv_free(char **, size_t);
static void yyerror(const char *);
int yywrap(void);

static struct mailz_conf *conf;
static const char *filename;
%}

%union {
	char string[1000];
	int number;
	struct {
		char **argv;
		size_t argc;
	} argv;
}

%token ADDRESS IGNORE OVERLONG RETAIN
%token<string> STRING
%type<argv> strings
%type<number> ignore_type
%%
grammar: /* empty */
	| grammar address '\n'
	| grammar ignore '\n'
	| grammar '\n';
	;

address: ADDRESS STRING {
		if (strlcpy(conf->address, $2,
			    sizeof(conf->address))
			    >= sizeof(conf->address)) {
			yyerror("address too long");
			YYERROR;
		}
	}
	;

ignore_type: IGNORE { $$ = MAILZ_IGNORE_IGNORE; }
	| RETAIN { $$ = MAILZ_IGNORE_RETAIN; }
	;

ignore: ignore_type strings {
		conf->ignore.type = $1;

		argv_free(conf->ignore.headers, conf->ignore.nheader);
		conf->ignore.headers = $2.argv;
		conf->ignore.nheader = $2.argc;
	}
	;

strings: STRING {
		$$.argv = reallocarray(NULL, 1, sizeof(*$$.argv));
		if ($$.argv == NULL) {
			warn(NULL);
			YYABORT;
		}

		if (($$.argv[0] = strdup($1)) == NULL) {
			warn(NULL);
			free($$.argv);
			YYABORT;
		}
		$$.argc = 1;
	}
	| strings STRING {
		if ($1.argc == SIZE_MAX) {
			warnc(ENOMEM, NULL);
			argv_free($1.argv, $1.argc);
			YYABORT;
		}

		$$.argv = reallocarray($1.argv, $1.argc + 1,
				       sizeof(*$$.argv));
		if ($$.argv == NULL) {
			warn(NULL);
			argv_free($1.argv, $1.argc);
			YYABORT;
		}

		if (($$.argv[$1.argc] = strdup($2)) == NULL) {
			warn(NULL);
			argv_free($$.argv, $1.argc);
			YYABORT;
		}
		$$.argc = $1.argc + 1;
	}
	;
%%

static void
argv_free(char **argv, size_t argc)
{
	size_t i;

	for (i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
}

static int
configure(struct mailz_conf *c, const char *path)
{
	FILE *fp;
	int rv;

	rv = -1;

	if ((fp = fopen(path, "r")) == NULL) {
		if (errno != ENOENT)
			return -1;
		return 0;
	}

	conf = c;
	filename = path;
	yyin = fp;

	if (yyparse() != 0)
		goto fp;

	rv = 0;
	fp:
	fclose(fp);

	conf = NULL;
	filename = NULL;
	yyin = NULL;
	return rv;
}

void
mailz_conf_free(struct mailz_conf *c)
{
	argv_free(c->ignore.headers, c->ignore.nheader);
}

int
mailz_conf_init(struct mailz_conf *c)
{
	struct passwd *pw;
	char path[PATH_MAX], *pathp;

	memset(c, 0, sizeof(*c));

	if ((pw = getpwuid(getuid())) == NULL)
		return -1;

	if ((pathp = getenv("MAILZ_CONF")) == NULL) {
		int n;

		n = snprintf(path, sizeof(path), "%s/.mailz.conf",
			     pw->pw_dir);
		if (n < 0 || (size_t)n >= sizeof(path))
			return -1;
		pathp = path;
	}

	if (strlcpy(c->address, pw->pw_name, sizeof(c->address))
		    >= sizeof(c->address))
		return -1;

	if (configure(c, pathp) == -1)
		return -1;
	return 0;
}

static void
yyerror(const char *s)
{
	fprintf(stderr, "%s: %s on line %d\n", filename, s, yylineno);
}

int
yywrap(void)
{
	return 1;
}
