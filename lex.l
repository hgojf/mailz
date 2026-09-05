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

void yyerror(const char *);

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
	if (strlcpy(yylval.string, yytext, sizeof(yylval.string))
		   >= sizeof(yylval.string)) {
		yyerror("overly long string");
		exit(1);
	}
	return STRING;
}

\"[^\n\"]*\" {
	yytext[yyleng - 1] = '\0';
	if (strlcpy(yylval.string, yytext + 1, sizeof(yylval.string))
		   >= sizeof(yylval.string)) {
		yyerror("overly long string");
		exit(1);
	}
	return STRING;
}

[ \t] { }
#[^\n]* { }
\\\n { }

.|\n { return yytext[0]; }
%%
