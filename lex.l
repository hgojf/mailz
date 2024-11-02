%{
#include <string.h>

#include "conf-parse.h"
%}

%option noinput nounput
%option yylineno
%%
"address" { return ADDRESS; }
"ignore" { return IGNORE; }
"retain" { return RETAIN; }

[a-zA-Z-]+ {
	if (strlcpy(yylval.string, yytext, sizeof(yylval.string))
		    >= sizeof(yylval.string))
		return OVERLONG;
	return STRING;
}

"\""[^\n\"]*"\"" {
	yytext[yyleng - 1] = '\0';
	if (strlcpy(yylval.string, yytext + 1,
		    sizeof(yylval.string)) >= sizeof(yylval.string))
		return OVERLONG;

	return STRING;
}

[ \t] { }
"#"[^\n]* { }

.|\n { return yytext[0]; }
%%
