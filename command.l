%{
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "../argv.h"
#include "../edit.h"
#include "../config.h"
#include "command.h"

%}
%option yylineno
%%
"address" { return ADDRESS; }
"cache" { return CACHE; }
"edit" { return EDIT; }
"ignore" { return IGNORE; }
"manual" { return MANUAL; }
"more" { return MORE; }
"p" { return PRINT; }
"print" { return PRINT; }
"r" { return READ; }
"read" { return READ; }
"reorder" { return REORDER; }
"s" { return SAVE; }
"save" { return SAVE; }
"send" { return SEND; }
"set" { return SET; }
"t" { return THREAD; }
"thread" { return THREAD; }
"unignore" { return UNIGNORE; }
"vi" { return VI; }
"x" { return UNREAD; }

[a-zA-Z_@\.-]+[a-zA-Z0-9_@\.-]* { 
	yylval.string = strdup(yytext); 
	if (yylval.string == NULL)
		return 0;
	return STRING;
}
"\""[ \ta-zA-Z_@\.\-]+[ \ta-zA-Z0-9_@\.-]*"\"" { 
	yylval.string = strndup(yytext + 1, yyleng - 2); 
	if (yylval.string == NULL)
		return 0;
	return STRING;
}

[0-9]+ {
	const char *errstr;

	yylval.number = strtonum(yytext, 0, LLONG_MAX, &errstr);
	if (errstr != NULL) {
		fprintf(stderr, "number was %s", errstr);
		return 0;
	}
	return NUMBER;
}

[ \t] { } /* ignore */
\n { return '\n'; }
. { return yytext[0]; }
%%
