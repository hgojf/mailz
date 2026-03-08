#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "command.h"

void
command_init(struct command_lexer *lex, FILE *fp)
{
	lex->eol = 1;
	lex->fp = fp;
}

int
command_letter(struct command_lexer *lex, struct command_letter *lp)
{
	size_t n;
	long long num;
	int thread;
	char buf[21];
	const char *errstr;

	if (lex->eol)
		return COMMAND_EOF;

	n = 0;
	thread = 0;
	for (;;) {
		int ch;

		if ((ch = fgetc(lex->fp)) == EOF) {
			if (n == 0) {
				if (thread)
					return COMMAND_THREAD_EOF;
				return COMMAND_EOF;
			}
			break;
		}

		if ((ch == ' ' || ch == '\t') && n != 0)
			break;
		if (ch == '\n') {
			lex->eol = 1;
			if (n == 0) {
				if (thread)
					return COMMAND_THREAD_EOF;
				return COMMAND_EOF;
			}
			break;
		}

		if (ch == 't' && n == 0) {
			thread = 1;
			continue;
		}

		if (n == sizeof(buf))
			return COMMAND_LONG;
		buf[n++] = ch;
	}

	if (n == sizeof(buf))
		return COMMAND_LONG;
	buf[n] = '\0';

	num = strtonum(buf, 0, LLONG_MAX, &errstr);
	if (errstr != NULL)
		return COMMAND_INVALID;

	lp->num = num;
	lp->thread = thread;
	return COMMAND_OK;
}

int
command_name(struct command_lexer *lex, char *buf, size_t bufsz)
{
	size_t n;

	/*
	 * If the previous calls didnt read the entire line then
	 * we skip whatever they didnt read.
	 */
	if (!lex->eol) {
		int ch;

		while ((ch = fgetc(lex->fp)) != EOF && ch != '\n')
			;
	}
	lex->eol = 0;

	n = 0;
	for (;;) {
		int ch;

		if ((ch = fgetc(lex->fp)) == EOF) {
			if (n == 0)
				return COMMAND_EOF;
			break;
		}

		if ((ch == ' ' || ch == '\t') && n != 0)
			break;
		if (ch == '\n') {
			lex->eol = 1;
			if (n == 0)
				return COMMAND_EMPTY;
			break;
		}

		if (n == bufsz)
			return COMMAND_LONG;
		buf[n++] = ch;
	}

	if (n == bufsz)
		return COMMAND_LONG;
	buf[n] = '\0';
	return COMMAND_OK;
}
