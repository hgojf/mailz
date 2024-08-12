#ifndef MAILZ_LETTER_H
#define MAILZ_LETTER_H
struct from {
	char *addr;
	char *name;
};

struct letter {
	time_t date;
	struct from from;
	char *message_id;
	char *path;
	char *subject;
};

struct mailbox {
	struct letter *letters;
	size_t nletter;
};

void letter_free(struct letter *);
int letter_print(size_t, const struct letter *);
int letter_seen(const char *);
#endif /* MAILZ_LETTER_H */
