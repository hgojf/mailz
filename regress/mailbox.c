#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../mailbox.h"
#include "mailbox.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

void
mailbox_thread_test(void)
{
	size_t i;
	const struct {
		char **subjects;
		size_t nsubject;
		size_t *matches;
		size_t letter;
	} tests[] = {
		#define matches(...) (size_t []) { __VA_ARGS__, SIZE_MAX }
		#define subjects(...) (char *[]) { __VA_ARGS__ }, \
					nitems(((char *[]) { __VA_ARGS__ }))

		{ subjects("hi", "Re: hi", "wazzap"), matches(0, 1), 0 },
		{ subjects("wazzap", "hi", "Re: hi"), matches(1, 2), 1 },
		{ subjects("hi", "hi", "Re: hi"), matches(0), 0 },
		{ subjects(NULL, NULL), matches(0), 0 },

		#undef matches
		#undef subject
	};

	for (i = 0; i < nitems(tests); i++) {
		struct mailbox mailbox;
		struct mailbox_thread thread;
		size_t j, *m;

		mailbox_init(&mailbox);

		for (j = 0; j < tests[i].nsubject; j++) {
			struct letter letter;

			letter.date = 0;
			letter.from = "bogus";
			letter.path = "bogus";
			letter.subject = tests[i].subjects[j];

			if (mailbox_add_letter(&mailbox, &letter) == -1)
				err(1, "mailbox_add_letter");

		}

		mailbox_thread_init(&mailbox, &thread,
				    &mailbox.letters[tests[i].letter]);

		for (m = tests[i].matches; *m != SIZE_MAX; m++) {
			struct letter *letter;

			if ((letter = mailbox_thread_next(&mailbox, &thread)) == NULL)
				errx(1, "early end of thread");

			if ((size_t)(letter - mailbox.letters) != *m)
				errx(1, "wrong letter");
		}

		mailbox_free(&mailbox);
	}
}
