#ifndef MAILZ_READ_LETTER_H
#define MAILZ_READ_LETTER_H
#include <sys/queue.h>

#include <imsg.h>

struct read_letter {
	FILE *e;
	FILE *o;
	pid_t pid;
};

int maildir_read_letter_close(struct read_letter *);
int maildir_read_letter(struct read_letter *, int, int, int, 
	struct ignore *, struct reorder *);
int maildir_read_letter_getc(struct read_letter *, char [static 4]);
#endif /* MAILZ_READ_LETTER_H */
