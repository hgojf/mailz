%{
#include <err.h>

#include "parse.h"
%}

%option nounput
%option noinput
%option yylineno
%%
"address" { return ADDRESS; }
"cache" { return CACHE; }
"ignore" { return IGNORE; }
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

[ \t] { }
"#"[^\n]* { }

.|\n { return yytext[0]; }
%%
