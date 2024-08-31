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
"max" { return MAX; }
"min" { return MIN; }
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
