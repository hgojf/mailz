#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mailbox.h"

static int letter_date_cmp(const void *, const void *);

static int
letter_date_cmp(const void *one, const void *two)
{
	time_t n1, n2;

	n1 = ((const struct letter *)one)->date;
	n2 = ((const struct letter *)two)->date;

	if (n1 > n2)
		return 1;
	else if (n1 == n2)
		return 0;
	else
		return -1;
}

/*
 * Add a letter to the mailbox.
 * Returns 0 on success, returns -1 and sets errno on faillure.
 * Can fail and set errno for any of the reasons specified by malloc(3)
 * and reallocarray(3).
 */
int
mailbox_add_letter(struct mailbox *mailbox, struct letter *letter)
{
	struct letter copy, *letters;

	copy.date = letter->date;
	if ((copy.from = strdup(letter->from)) == NULL)
		return -1;
	if ((copy.path = strdup(letter->path)) == NULL)
		goto from;
	if (letter->subject != NULL) {
		if ((copy.subject = strdup(letter->subject)) == NULL)
			goto path;
	}
	else
		copy.subject = NULL;

	if (mailbox->nletter == SIZE_MAX) {
		errno = ENOMEM;
		goto subject;
	}
	letters = reallocarray(mailbox->letters, mailbox->nletter + 1,
			       sizeof(*mailbox->letters));
	if (letters == NULL)
		goto subject;

	mailbox->letters = letters;
	mailbox->letters[mailbox->nletter++] = copy;

	return 0;

	subject:
	free(copy.subject);
	path:
	free(copy.path);
	from:
	free(copy.from);
	return -1;
}

/*
 * Frees the memory associated with mailbox.
 * Any further use of mailbox is undefined.
 */
void
mailbox_free(struct mailbox *mailbox)
{
	size_t i;

	for (i = 0; i < mailbox->nletter; i++) {
		free(mailbox->letters[i].from);
		free(mailbox->letters[i].path);
		free(mailbox->letters[i].subject);
	}
	free(mailbox->letters);
}

/*
 * Initializes mailbox for use with the mailbox_* functions.
 * Use of mailbox with these functions before a call to mailbox_init
 * is undefined.
 */
void
mailbox_init(struct mailbox *mailbox)
{
	mailbox->letters = NULL;
	mailbox->nletter = 0;
}

/*
 * Sort the letters in mailbox by date in ascending order.
 */
void
mailbox_sort(struct mailbox *mailbox)
{
	qsort(mailbox->letters, mailbox->nletter,
	      sizeof(*mailbox->letters), letter_date_cmp);
}

/*
 * Initialize a thread iterator to find all messages in the same
 * thread as letter.
 * If letter is not a member of mailbox->letters the behaviour is
 * undefined.
 */
void
mailbox_thread_init(struct mailbox *mailbox,
		    struct mailbox_thread *thread,
		    struct letter *letter)
{
	thread->have_first = 0;
	if (letter->subject != NULL && !strncmp(letter->subject, "Re: ", 4)) {
		thread->idx = 0;
		thread->subject = &letter->subject[4];
	}
	else {
		thread->idx = letter - mailbox->letters;
		thread->subject = letter->subject;
	}
	thread->letter = letter;

}

/*
 * Get the next letter from the thread.
 * Returns NULL if there are no more letters in the thread.
 */
struct letter *
mailbox_thread_next(struct mailbox *mailbox,
		    struct mailbox_thread *thread)
{
	size_t i;

	if (thread->subject == NULL) {
		struct letter *letter;

		letter = thread->letter;
		thread->letter = NULL;
		return letter;
	}

	for (i = thread->idx; i < mailbox->nletter; i++) {
		const char *subject;

		subject = mailbox->letters[i].subject;
		if (subject == NULL)
			continue;

		if (!strncmp(subject, "Re: ", 4)) {
			if (!strcmp(&subject[4], thread->subject)) {
				thread->idx = i + 1;
				return &mailbox->letters[i];
			}
		}

		if (!strcmp(subject, thread->subject)) {
			if (thread->have_first) {
				thread->idx = mailbox->nletter;
				return NULL;
			}

			thread->have_first = 1;
			thread->idx = i + 1;
			return &mailbox->letters[i];
		}
	}

	return NULL;
}
