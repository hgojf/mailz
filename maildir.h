#ifndef MAILZ_MAILDIR_H
#define MAILZ_MAILDIR_H
int maildir_setup(const char *, int);

struct maildir_read {
	size_t nletters;
	struct letter *letters;
	int need_recache;
};

int maildir_read(char *, int, int, struct maildir_read *);

int
maildir_read_letter(const char *, const char *, int, FILE *, int, 
struct argv_shm *, struct argv_shm *);
#endif /* MAILZ_MAILDIR_H */
