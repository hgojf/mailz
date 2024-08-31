#ifndef MAILZ_CHARSET_H
#define MAILZ_CHARSET_H
#include <wchar.h>

#include "encoding.h"

struct charset {
	enum charset_type {
		CHARSET_ASCII,
		CHARSET_OTHER,
		CHARSET_UTF8,
	} type;

	union {
		mbstate_t utf8;
	} val;

	struct encoding encoding;
};

int charset_getchar(FILE *, struct charset *, char [static 4]);
int charset_set(struct charset *, const char *, size_t);
void charset_set_type(struct charset *, enum charset_type);
#endif /* MAILZ_CHARSET_H */
