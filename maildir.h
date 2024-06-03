#ifndef MAILZ_MAILDIR_H
#define MAILZ_MAILDIR_H
struct maildir_letter {
	char *path;
	char *subject;
	char *from;
	time_t date;
};

struct maildir {
	DIR *cur;

	long long nletters;
	struct maildir_letter *letters;
};

int maildir_setup(int, struct maildir *);
int maildir_read(struct maildir *, const struct options *);
int maildir_print(struct maildir *, size_t, size_t);
int maildir_letter_set_flag(struct maildir *, struct maildir_letter *, char);
int maildir_letter_print(size_t nth, struct maildir_letter *);
int maildir_letter_print_read(struct maildir *, struct maildir_letter *, 
const struct options *, FILE *);
void maildir_free(struct maildir *);
#endif /* MAILZ_MAILDIR_H */
