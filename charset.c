#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "charset.h"
#include "string-util.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

static int utf8_getchar(mbstate_t *, struct encoding *, FILE *, 
	char [static 4]);

int
charset_getchar(FILE *fp, struct charset *charset, char buf[static 4])
{
	int c;

	if (charset->type == CHARSET_UTF8)
		return utf8_getchar(&charset->val.utf8, &charset->encoding, fp, buf);

	if ((c = encoding_getbyte(fp, &charset->encoding)) == ENCODING_ERR)
		return -1;
	if (c == ENCODING_EOF)
		return 0;

	switch (charset->type) {
	case CHARSET_ASCII:
		if (!isascii(c))
			return -1;
		if (!isprint(c) && !isspace(c))
			goto replace;
		break;
	case CHARSET_OTHER:
		if (!isascii(c) || (!isprint(c) && !isspace(c)))
			goto replace;
		break;
	case CHARSET_UTF8:
		assert(0);
	}

	buf[0] = c;
	return 1;

	replace:
	/* utf8 replacement character */
	memcpy(buf, "\xEF\xBF\xBD", 3);
	return 3;
}

int
charset_set(struct charset *charset, const char *type, size_t tl)
{
	struct {
		const char *ident;
		enum charset_type type;
	} charsets[] = {
		{ "utf-8", CHARSET_UTF8 },
		{ "us-ascii", CHARSET_ASCII },
	};

	for (size_t i = 0; i < nitems(charsets); i++) {
		if (bounded_strcasecmp(charsets[i].ident, type, tl) != 0)
			continue;

		charset_set_type(charset, charsets[i].type);
		return 0;
	}

	return -1;
}

void
charset_set_type(struct charset *charset, enum charset_type type)
{
	memset(&charset->val, 0, sizeof(charset->val));
	charset->type = type;
}

static int
utf8_getchar(mbstate_t *mbs, struct encoding *encoding, FILE *fp, char buf[static 4])
{
	int c;

	for (int i = 0; i < 4; i++) {
		if ((c = encoding_getbyte(fp, encoding)) == ENCODING_ERR)
			return -1;
		if (c == ENCODING_EOF) {
			if (i == 0)
				return 0;
			return -1;
		}

		buf[i] = c;
		switch (mbrtowc(NULL, &buf[i], 1, mbs)) {
		case (size_t)-1:
		case (size_t)-3:
			return -1;
		case 0:
			goto replace;
		case (size_t)-2:
			break;
		default:
			if (i == 0 && !isprint(c) && !isspace(c))
				goto replace;
			return i + 1;
		}
	}

	return -1;

	replace:
	/* utf8 replacement character */
	memcpy(buf, "\xEF\xBF\xBD", 3);
	return 3;
}
