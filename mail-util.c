#include <sys/tree.h>

#include <stdlib.h>
#include <string.h>

#include "mail.h"
#include "mail-util.h"

int
header_cmp(struct header *one, struct header *two)
{
	return strcmp(one->key, two->key);
}

RB_GENERATE(headers, header, entries, header_cmp);

void
letter_free(struct letter *letter)
{
	struct header *n1, *n2;
	RB_FOREACH_SAFE(n1, headers, &letter->headers, n2) {
		RB_REMOVE(headers, &letter->headers, n1);
		free(n1->key);
		free(n1->val);
		free(n1);
	}
	free(letter->text);
}

void
mail_free(struct mail *mail)
{
	for (size_t i = 0; i < mail->nletters; i++) {
		letter_free(&mail->letters[i]);
	}
	free(mail->letters);
}

const char *
header_find(struct letter *letter, char *key)
{
	struct header *hp, header;

	header.key = key;

	if ((hp = RB_FIND(headers, &letter->headers, &header)) == NULL)
		return NULL;
	return hp->val;
}
