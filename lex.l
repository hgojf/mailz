%{
#include <err.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "parse.h"
%}

%option nounput
%option noinput
%option yylineno
%%
"address" { return ADDRESS; }
"cache" { return CACHE; }
"ignore" { return IGNORE; }
"linewrap" { return LINEWRAP; }
"no" { return NO; }
"reorder" { return REORDER; }
"retain" { return RETAIN; }

[a-zA-Z\.@-]+[a-zA-Z0-9\.@-]* {
	if ((yylval.string = strdup(yytext)) == NULL) {
		warn(NULL);
		return OOM;
	}
	return STRING;
}

"\""[a-zA-Z0-9\.@<> \t-]+"\"" {
	if ((yylval.string = strndup(yytext + 1, yyleng - 2)) == NULL) {
		warn(NULL);
		return OOM;
	}
	return STRING;
}

[0-9]+ {
	const char *errstr;

	yylval.number = strtonum(yytext, 0, LLONG_MAX, &errstr);
	if (errstr != NULL)
		return BADNUM;
	return NUMBER;
}

"\\"\n { }

[ \t] { }
"#"[^\n]* { }

.|\n { return yytext[0]; }
%%
