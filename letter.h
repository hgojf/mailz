#ifndef MAILZ_LETTER_H
#define MAILZ_LETTER_H
struct letter {
	struct from_safe from;
	char *path;
	char *subject;
	time_t date;
};
#endif /* MAILZ_LETTER_H */
