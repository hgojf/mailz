#ifndef MAILZ_MAILDIR_H
#define MAILZ_MAILDIR_H
struct maildir_setup {
	int status;
	int save_errno;
};

struct maildir_setup maildir_setup(const char *, int);

struct maildir_send {
	int status;
	int save_errno;
};

struct maildir_send maildir_send(const char *, const char *, const char *, 
	int, int);

struct maildir_read {
	int status;
	union {
		struct {
			size_t nletters;
			struct letter *letters;
		} good;
		int save_errno; /* bad */
	} val;
};

struct maildir_read maildir_read(const char *, int, int);

struct maildir_read_letter {
	int status;
	int save_errno;
};

struct maildir_read_letter
maildir_read_letter(const char *, const char *, int, FILE *, int, 
struct argv_shm *, struct argv_shm *);
#endif /* MAILZ_MAILDIR_H */
