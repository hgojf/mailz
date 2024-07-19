#ifndef MAILZ_MAILDIR_H
#define MAILZ_MAILDIR_H
int maildir_setup(const char *, int);

struct maildir_read {
	int status;
	union {
		struct {
			size_t nletters;
			struct letter *letters;
			int need_recache;
		} good;
		int save_errno; /* bad */
	} val;
};

struct maildir_read maildir_read(char *, int, int);

struct maildir_read_letter {
	int status;
	int save_errno;
};

struct maildir_read_letter
maildir_read_letter(const char *, const char *, int, FILE *, int, 
struct argv_shm *, struct argv_shm *);
#endif /* MAILZ_MAILDIR_H */
