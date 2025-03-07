#ifndef MAILBOX_H
#define MAILBOX_H

struct letter {
	char *from;
	char *path;
	char *subject;
	time_t date;
};

struct mailbox {
	struct letter *letters;
	size_t nletter;
};

struct mailbox_thread {
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
