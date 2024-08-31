#ifndef MAILZ_LETTER_H
#define MAILZ_LETTER_H
#include <time.h>

struct letter {
	time_t date;
	struct {
		char *addr;
		char *name;
	} from;
	char *path;
	char *subject;
};

void letter_free(struct letter *);
#endif /* MAILZ_LETTER_H */
