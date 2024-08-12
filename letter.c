#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "letter.h"

int
letter_seen(const char *path)
{
	const char *flags;

	return (flags = strstr(path, ":2,")) != NULL && strchr(flags, 'S') != NULL;
}

int
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

void
letter_free(struct letter *letter)
{
	free(letter->from.addr);
	free(letter->from.name);
	free(letter->message_id);
	free(letter->path);
	free(letter->subject);
}
