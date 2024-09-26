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
#include <limits.h>
#include <stdlib.h>

#include "parse.h"
%}

%option nounput noinput
%option yylineno
%%
"address" { return ADDRESS; }
"cache" { return CACHE; }
"ignore" { return IGNORE; }
"linewrap" { return LINEWRAP; }
"no" { return NO; }
"reorder" { return REORDER; }
"retain" { return RETAIN; }

-?[0-9]+ {
	const char *errstr;

	yylval.number = strtonum(yytext, LLONG_MIN, LLONG_MAX, &errstr);
	if (errstr != NULL)
		return BADNUM;

	return NUMBER;
}

[a-zA-Z0-9\.@-]+ {
	if ((yylval.string = strdup(yytext)) == NULL)
		return OOM;
	return STRING;
}

"\""[^\"\n]*"\"" {
	if ((yylval.string = strndup(yytext + 1, yyleng - 2)) == NULL)
		return OOM;
	return STRING;
}

"\\"\n { }
[ \t] { }
"#"[^\n]* { }

.|\n { return yytext[0]; }
%%
