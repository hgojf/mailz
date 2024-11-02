#include <string.h>
#include <wchar.h>

#include "charset.h"
#include "encoding.h"

#define CTR_ASCII 0x1

static int charset_raw(struct encoding *, FILE *, int, char [static 4]);
static int charset_utf8(struct charset_utf8 *, struct encoding *,
			FILE *, char [static 4]);

void
charset_from_type(struct charset *c, enum charset_type type)
{
	switch (type) {
	case CHARSET_ASCII:
	case CHARSET_OTHER:
		break;
	case CHARSET_UTF8:
		memset(&c->v.utf8, 0, sizeof(c->v.utf8));
		break;
	}

	c->type = type;
}

int
charset_getc(struct charset *c, struct encoding *encoding, FILE *fp,
	     char buf[static 4])
{
	switch (c->type) {
	case CHARSET_ASCII:
		return charset_raw(encoding, fp, CTR_ASCII, buf);
	case CHARSET_OTHER:
		return charset_raw(encoding, fp, 0, buf);
	case CHARSET_UTF8:
		return charset_utf8(&c->v.utf8, encoding, fp, buf);
	}

	return -1;
}

static int
charset_raw(struct encoding *e, FILE *fp, int flags,
	    char buf [static 4])
{
	int ch;

	if ((ch = encoding_getc(e, fp)) == ENCODING_ERR)
		return -1;
	if (ch == ENCODING_EOF)
		return 0;

	if (flags & CTR_ASCII && ch > 127)
		return -1;

	buf[0] = ch;
	return 1;
}

static int
charset_utf8(struct charset_utf8 *utf8, struct encoding *e,
	     FILE *fp, char buf[static 4])
{
	int i;

	for (i = 0; i < 3; i++) {
		int ch;
		char cc;

		if ((ch = encoding_getc(e, fp)) == ENCODING_ERR)
			return -1;
		if (ch == ENCODING_EOF) {
			if (i == 0)
				return 0;
			return -1;
		}

		cc = ch;

		switch (mbrtowc(NULL, &cc, 1, &utf8->mbs)) {
		case -1:
		case -3:
			return -1;
		case -2:
			buf[i] = ch;
			continue;
		default:
			buf[i] = ch;
			return i + 1;
		}
	}

	return -1;
}
