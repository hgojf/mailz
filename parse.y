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
#include <sys/tree.h>

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
static int config_mailbox_cmp(struct config_mailbox *, struct config_mailbox *);
static char *maildir_expand(const char *);

void yyerror(const char *);
int yywrap(void);

RB_PROTOTYPE_STATIC(config_mailboxes, config_mailbox, entries, config_mailbox_cmp)
RB_GENERATE_STATIC(config_mailboxes, config_mailbox, entries, config_mailbox_cmp)

static struct config *conf;
static const char *filename;
static struct config_mailbox *mailbox;
%}

%union {
	char string[1000];
	int number;
	struct {
		char **argv;
		size_t argc;
	} argv;
}

%token ADDRESS IGNORE MAILBOX MAILDIR OVERLONG PATH RETAIN
%token<string> STRING
%type<argv> strings
%type<number> ignore_retain
%%
grammar: /* empty */
	| grammar address '\n'
	| grammar ignore '\n'
	| grammar mailbox '\n'
	| grammar '\n'
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

mailbox: MAILBOX STRING {
		if ((mailbox = malloc(sizeof(*mailbox))) == NULL)
			err(1, NULL);
		if ((mailbox->ident = strdup($2)) == NULL)
			err(1, NULL);
		mailbox->address[0] = '\0';
		mailbox->maildir = NULL;
	} '{' mailbox_opts '}' {
		if (mailbox->maildir == NULL) {
			yyerror("mailbox without maildir");
			YYERROR;
		}
		if (RB_INSERT(config_mailboxes, &conf->mailboxes, mailbox) != NULL) {
			yyerror("duplicate mailbox name");
			YYERROR;
		}
		mailbox = NULL;
	}
	;

mailbox_address: ADDRESS STRING {
		if (strlcpy(mailbox->address, $2,
			    sizeof(mailbox->address))
			    >= sizeof(mailbox->address)) {
			yyerror("address too long");
			YYERROR;
		}
	}
	;

mailbox_maildir: MAILDIR PATH STRING {
		mailbox->maildir = maildir_expand($3);
	}
	;

mailbox_opts: /* empty */
	| mailbox_opts mailbox_address '\n'
	| mailbox_opts mailbox_maildir '\n'
	| mailbox_opts '\n'
	;

ignore_retain: IGNORE { $$ = 0; }
	| RETAIN { $$ = 1; }
	;

ignore: ignore_retain strings {
		conf->ignore.retain = $1;

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
config_mailbox_cmp(struct config_mailbox *one, struct config_mailbox *two)
{
	return strcmp(one->ident, two->ident);
}

static char *
maildir_expand(const char *maildir)
{
	const char *home;
	char *buf;
	size_t homelen, n, sz;

	if ((home = getenv("HOME")) == NULL)
		errx(1, "HOME not set");
	homelen = strlen(home);

	sz = strlen(maildir) + homelen + 1;
	if ((buf = malloc(sz)) == NULL)
		err(1, NULL);

	n = 0;
	for (;;) {
		if (*maildir == '~') {
			if (sz - n <= homelen) {
				sz += homelen;
				if ((buf = realloc(buf, sz)) == NULL)
					err(1, NULL);
			}
			memcpy(&buf[n], home, homelen);
			n += homelen;
		}
		else {
			if (sz == n) {
				sz += 1;
				if ((buf = realloc(buf, sz)) == NULL)
					err(1, NULL);
			}
			buf[n++] = *maildir;
			if (*maildir == '\0')
				return buf;
		}

		maildir++;
	}
}

void
config_free(struct config *cfg)
{
	struct config_mailbox *mb, *t;

	argv_free(cfg->ignore.headers, cfg->ignore.nheader);

	RB_FOREACH_SAFE(mb, config_mailboxes, &cfg->mailboxes, t) {
		RB_REMOVE(config_mailboxes, &cfg->mailboxes, mb);
		free(mb->ident);
		free(mb->maildir);
		free(mb);
	}
}

void
parse_config(struct config *cfg, const char *path)
{
	FILE *fp;
	char pathbuf[PATH_MAX];

	memset(cfg, 0, sizeof(*cfg));

	if (path == NULL) {
		int n;
		const char *home;

		if ((home = getenv("HOME")) == NULL)
			errx(1, "HOME not set");

		n = snprintf(pathbuf, sizeof(pathbuf), "%s/.mailz.conf", home);
		if (n < 0 || (size_t)n >= sizeof(pathbuf))
			errx(1, "snprintf");
		path = pathbuf;
	}

	if ((fp = fopen(path, "r")) == NULL) {
		if (errno == ENOENT)
			return;
		err(1, "%s", path);
	}

	conf = cfg;
	filename = path;
	yyin = fp;

	if (yyparse() != 0)
		exit(1); /* yyerror() gave an error message */
	fclose(fp);
}

struct config_mailbox *
config_mailbox(struct config *cfg, char *ident)
{
	struct config_mailbox mb;

	mb.ident = ident;
	return RB_FIND(config_mailboxes, &cfg->mailboxes, &mb);
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
