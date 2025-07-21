#ifndef MAILBOX_H
#define MAILBOX_H

struct letter {
	char *from;
	char *path;
	char *subject;
	char *thread;
	time_t date;
	int thread_is_reply;
};

struct mailbox {
	struct letter *letters;
	size_t nletter;
};

struct mailbox_thread {
	struct letter *letter;
	const char *subject;
	size_t idx;
	int have_first;
};

int mailbox_add_letter(struct mailbox *, struct letter *);
void mailbox_free(struct mailbox *);
void mailbox_init(struct mailbox *);
void mailbox_sort(struct mailbox *);
void mailbox_thread_init(struct mailbox *, struct mailbox_thread *,
			 struct letter *);
struct letter *mailbox_thread_next(struct mailbox *,
				   struct mailbox_thread *);

#endif /* MAILBOX_H */
