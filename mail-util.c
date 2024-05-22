#include <sys/tree.h>

#include <ctype.h>
#include <stdio.h>
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

int
header_read(FILE *fp, char **lp, size_t *np, struct header *out)
{
	size_t vlen;

	if ((out->val = strchr(*lp, ':')) == NULL)
		return -1;
	*out->val++ = '\0';
	/* strip trailing ws */
	out->val += strspn(out->val, " \t");

	out->key = *lp;
	/* strip trailing ws */
	out->key[strcspn(out->key, " \t")] = '\0';

	for (size_t i = 0; out->key[i] != '\0'; i++) {
		if (out->key[i] < 33 || out->key[i] > 126)
			return -1;
	}

	for (vlen = 0; out->val[vlen] != '\0'; vlen++) {
		if (out->val[vlen] > 127)
			return -1;
	}

	if ((out->key = strdup(out->key)) == NULL)
		return -1;
	if ((out->val = strndup(out->val, vlen)) == NULL)
		goto key;

	for (;;) {
		char c, *line;
		void *t;
		ssize_t len, ws;

		if ((c = fgetc(fp)) == EOF)
			goto val;
		if (!isspace(c) || c == '\n') {
			if (ungetc(c, fp) == EOF)
				goto val;
			break;
		}

		if ((len = getline(lp, np, fp)) == -1)
			goto val;
		if ((*lp)[len - 1] == '\n') {
			(*lp)[len - 1] = '\0';
			len--;
		}

		ws = strspn(*lp, " \t");
		len -= ws;
		line = (*lp) + ws;

		for (size_t i = 0; line[i] != '\0'; i++) {
			if (line[i] > 127)
				goto val;
		}

		t = realloc(out->val, vlen + len + 1);
		if (t == NULL)
			goto val;
		out->val = t;
		memcpy(&out->val[vlen], line, len);
		out->val[vlen + len] = '\0';
	}

	return 0;

	val:
	free(out->val);
	key:
	free(out->key);
	return -1;
}
