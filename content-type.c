#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "content-type.h"

static int dequote(const char **, size_t *);

int
content_type_init(struct content_type *type, const char *s)
{
	const char *e, *m;
	size_t len;

	if ((e = strchr(s, ';')) != NULL)
		len = (size_t)(e - s);
	else
		len = strlen(s);

	for (size_t i = 0; i < len; i++)
		if (isspace((unsigned char)s[i]))
			return -1;

	if ((m = memchr(s, '/', len)) == NULL)
		return -1;

	type->type = s;
	type->type_len = (size_t)(m - s);

	type->subtype = m + 1;

	type->subtype_len = (size_t)(&s[len - 1] - m);
	if (s[len - 1] == ';')
		type->subtype_len -= 1;

	type->rest = s + len;
	if (*type->rest == ';')
		type->rest++;

	return 0;
}

/*
 * Grabs the next variable from the Content-Type header.
 * returns -1 if a parsing error occured,
 * returns 0 if no variables are left,
 * otherwise returns nonzero.
 */
int
content_type_next(struct content_type *type, 
	struct content_type_var *var)
{
	const char *e, *eq;
	size_t vl;

	while (isspace((unsigned char)*type->rest))
		type->rest++;
	if (*type->rest == '\0')
		return 0;

	if ((e = strchr(type->rest, ';')) != NULL)
		vl = (size_t)(e - type->rest);
	else
		vl = strlen(type->rest);

	if ((eq = memchr(type->rest, '=', vl)) == NULL)
		return -1;

	var->key = type->rest;
	var->key_len = (size_t)(eq - type->rest);

	var->val = eq + 1;
	var->val_len = (size_t)(&type->rest[vl] - eq) - 1;

	if (dequote(&var->val, &var->val_len) == -1)
		return -1;

	type->rest += vl;

	/* if this is not NUL, then it is our terminating ';' */
	if (*type->rest != '\0')
		type->rest++;

	return 1;
}

/* XXX: this should be done during header parsing */
static int
dequote(const char **s, size_t *len)
{
	if ((*s)[0] == '\"') {
		if ((*s)[*len - 1] != '\"')
			return -1;
		*s += 1;
		*len -= 2;
	}

	return 0;
}
