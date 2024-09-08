/*
 * Copyright (c) 2024 Henry Ford <fordhenry2299@gmail.com>

 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.

 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extract.h"
#include "letter.h"
#include "list.h"

static int letter_print(size_t, const struct letter *);

int
list_letters(struct letter *letters, size_t start, size_t end)
{
	for (; start < end; start++)
		if (letter_print(start + 1, &letters[start]) == -1)
			return -1;
	return 0;
}

static int
letter_print(size_t nth, const struct letter *letter)
{
	struct tm tm;
	char date[33];
	const char *subject;

	if (localtime_r(&letter->date, &tm) == NULL)
		return -1;

	if (strftime(date, sizeof(date), "%a %b %d %H:%M", &tm) == 0)
		return -1;

	if ((subject = letter->subject) == NULL)
		subject = "No Subject";

	if (printf("%4zu %-20s %-32s %-30s\n", nth, date, 
		letter->from.addr, subject) < 0)
			return -1;
	return 0;
}

/*
 * This stuff is based on heuristics, but it is generally correct
 * while being simple and reasonably fast.
 */
int
thread_print(struct letter *letter, struct letter *letters, size_t nletter)
{
	const char *subject;
	size_t i;
	int havefirst;

	if (letter->subject == NULL)
		return letter_print(letter - letters, letter);

	if (strncmp(letter->subject, "Re: ", 4) == 0) {
		i = 0;
		subject = letter->subject + 4;
	}
	else {
		i = letter - letters;
		subject = letter->subject;
	}

	havefirst = 0;
	for (; i < nletter; i++) {
		if (letters[i].subject == NULL)
			continue;

		if (!strcmp(letters[i].subject, subject)) {
			if (havefirst) /* new thread */
				break;
			if (letter_print(i + 1, &letters[i]) == -1)
				return -1;
			havefirst = 1;
			continue;
		}

		if (strncmp(letters[i].subject, "Re: ", 4) != 0
			|| strcmp(letters[i].subject + 4, subject) != 0)
				continue;
		if (letter_print(i + 1, &letters[i]) == -1)
			return -1;
	}

	return 0;
}
