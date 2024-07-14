#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "address.h"
#include "letter.h"
#include "maildir-cache.h"
#include "maildir-cache-write.h"

static int letter_cmp_subject(const void *, const void *);

int
maildir_cache_write(FILE *out, struct letter *letters, size_t nletters,
	int view_all)
{
	uint32_t version;
	uint8_t view_seen;

	qsort(letters, nletters, sizeof(*letters), letter_cmp_subject);

	version = MAILDIR_CACHE_MAGIC | MAILDIR_CACHE_VERSION;
	if (fwrite(&version, sizeof(version), 1, out) != 1)
		return -1;

	view_seen = view_all;
	if (fwrite(&view_seen, sizeof(view_seen), 1, out) != 1)
		return -1;

	for (size_t i = 0; i < nletters; i++) {
		uint64_t date;
		const struct letter *letter = &letters[i];

		if (letter->date > UINT64_MAX)
			return -1;
		date = letter->date;

		if (fwrite(&date, sizeof(date), 1, out) != 1)
			return -1;

		if (fwrite(letter->path, strlen(letter->path) + 1, 1, out) != 1)
			return -1;
		if (letter->subject != NULL) {
			if (fwrite(letter->subject, strlen(letter->subject) + 1, 1, out) != 1)
				return -1;
		}
		else {
			if (fputc('\0', out) == EOF)
				return -1;
		}

		if (fwrite(letter->from.str, strlen(letter->from.str) + 1, 1, out) != 1)
			return -1;
	}

	return 0;
}

static int
letter_cmp_subject(const void *one, const void *two)
{
	const struct letter *n1 = one, *n2 = two;
	return strcmp(n1->path, n2->path);
}
