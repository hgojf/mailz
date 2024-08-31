#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "extract.h"
#include "letter.h"
#include "maildir.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

#define HEADER_DATE 0
#define HEADER_FROM 1
#define HEADER_SUBJECT 2

void
letter_free(struct letter *letter)
{
	free(letter->from.addr);
	free(letter->from.name);
	free(letter->path);
	free(letter->subject);
}
