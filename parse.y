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
static int conf_mailbox_cmp(struct mailz_conf_mailbox *, struct mailz_conf_mailbox *);
static char *maildir_expand(const char *);
static void yyerror(const char *);
int yywrap(void);

RB_PROTOTYPE_STATIC(mailz_conf_mailboxes, mailz_conf_mailbox, entries, conf_mailbox_cmp)
RB_GENERATE_STATIC(mailz_conf_mailboxes, mailz_conf_mailbox, entries, conf_mailbox_cmp)

static struct mailz_conf *conf;
static const char *filename;
static struct mailz_conf_mailbox *mailbox;
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
%type<number> ignore_type
%%
grammar: /* empty */
	| grammar address '\n'
	| grammar ignore '\n'
	| grammar mailbox '\n'
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
		if (RB_INSERT(mailz_conf_mailboxes, &conf->mailboxes, mailbox) != NULL) {
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
	mailbox = NULL;
	yyin = fp;

	if (yyparse() != 0)
		goto fp;

	rv = 0;
	fp:
	fclose(fp);

	conf = NULL;
	filename = NULL;
	mailbox = NULL;
	yyin = NULL;
	return rv;
}

static int
conf_mailbox_cmp(struct mailz_conf_mailbox *one, struct mailz_conf_mailbox *two)
{
	return strcmp(one->ident, two->ident);
}

static char *
maildir_expand(const char *maildir)
{
	struct passwd *pwd;
	char *buf;
	size_t dirlen, n, sz;

	if ((pwd = getpwuid(getuid())) == NULL)
		err(1, "getpwuid");
	dirlen = strlen(pwd->pw_dir);

	sz = strlen(maildir) + dirlen + 1;
	if ((buf = malloc(sz)) == NULL)
		err(1, NULL);

	n = 0;
	for (;;) {
		if (*maildir == '~') {
			if (sz - n <= dirlen) {
				sz += dirlen;
				if ((buf = realloc(buf, sz)) == NULL)
					err(1, NULL);
			}
			memcpy(&buf[n], pwd->pw_dir, dirlen);
			n += strlen(pwd->pw_dir);
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
mailz_conf_free(struct mailz_conf *c)
{
	struct mailz_conf_mailbox *mb, *t;

	argv_free(c->ignore.headers, c->ignore.nheader);

	RB_FOREACH_SAFE(mb, mailz_conf_mailboxes, &c->mailboxes, t) {
		RB_REMOVE(mailz_conf_mailboxes, &c->mailboxes, mb);
		free(mb->ident);
		free(mb->maildir);
		free(mb);
	}
}

int
mailz_conf_init(struct mailz_conf *c)
{
	struct passwd *pw;
	char hostname[HOST_NAME_MAX + 1], path[PATH_MAX], *pathp;
	int n;

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

	if (gethostname(hostname, sizeof(hostname)) == -1)
		return -1;

	n = snprintf(c->address, sizeof(c->address), "%s@%s", pw->pw_name,
		     hostname);
	if (n < 0 || (size_t)n >= sizeof(c->address))
		return -1;

	if (configure(c, pathp) == -1)
		return -1;
	return 0;
}

struct mailz_conf_mailbox *
mailz_conf_mailbox(struct mailz_conf *c, char *ident)
{
	struct mailz_conf_mailbox mb;

	mb.ident = ident;
	return RB_FIND(mailz_conf_mailboxes, &c->mailboxes, &mb);
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
