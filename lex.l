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
#include <string.h>

#include "parse.h"
%}

%option noinput nounput
%option yylineno
%%
"address" { return ADDRESS; }
"ignore" { return IGNORE; }
"mailbox" { return MAILBOX; }
"maildir" { return MAILDIR; }
"path" { return PATH; }
"retain" { return RETAIN; }

[a-zA-Z-]+ {
	if ((size_t)yyleng >= sizeof(yylval.string))
		return OVERLONG;
	memcpy(yylval.string, yytext, yyleng);
	yylval.string[yyleng] = '\0';
	return STRING;
}

"\""[^\n\"]*"\"" {
	if ((size_t)yyleng - 2 >= sizeof(yylval.string))
		return OVERLONG;
	memcpy(yylval.string, &yytext[1], yyleng - 2);
	yylval.string[yyleng - 2] = '\0';
	return STRING;
}

[ \t] { }
"#"[^\n]* { }
"\\"\n { }

.|\n { return yytext[0]; }
%%
