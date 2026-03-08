#ifndef COMMAND_H
#define COMMAND_H

enum {
	COMMAND_OK,
	COMMAND_EMPTY,
	COMMAND_EOF,
	COMMAND_INVALID,
	COMMAND_LONG,
	COMMAND_THREAD_EOF,
};

struct command_letter {
	size_t num;
	int thread;
};

struct command_lexer {
	FILE *fp;
	int eol;
};

void command_init(struct command_lexer *, FILE *);
int command_letter(struct command_lexer *, struct command_letter *);
int command_name(struct command_lexer *, char *, size_t);

#endif /* COMMAND_H */
