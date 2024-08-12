#ifndef MAILZ_READ_LETTER_H
#define MAILZ_READ_LETTER_H
#include <wchar.h>

struct read_letter {
	mbstate_t mbs;
	FILE *o;
	pid_t pid;
};

int read_letter(int, const char *, struct ignore *, struct reorder *, 
	struct read_letter *);
int read_letter_close(struct read_letter *);
int read_letter_getc(struct read_letter *, char [static 4]);
int read_letter_quick(int, const char *, struct ignore *, struct reorder *,
	FILE *);
#endif /* MAILZ_READ_LETTER_H */
