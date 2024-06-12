#ifndef MAILZ_MAILBOX_H
#define MAILZ_MAILBOX_H
struct letter {
	char *subject;
	char *from;
	time_t date;
	union {
		char *maildir_path;
		long mbox_offset;
	} ident;
};

struct mailbox {
	#define MAILBOX_MAILDIR 0
	#define MAILBOX_MBOX 1
	int type;

	long long nletters;
	struct letter *letters;

	union {
		DIR *maildir_cur;
		FILE *mbox_file;
	} val;
};

void mailbox_free(struct mailbox *);
int mailbox_setup(int, struct mailbox *);
int mailbox_read(struct mailbox *, int);
int mailbox_print(struct mailbox *, size_t, size_t);
int mailbox_letter_print(size_t, struct letter *);
int mailbox_letter_print_read(struct mailbox *, struct letter *,
	const struct options *, FILE *);
int mailbox_letter_mark_read(struct mailbox *, struct letter *);
int mailbox_letter_mark_unread(struct mailbox *, struct letter *);
#endif /* MAILZ_MAILBOX_H */
