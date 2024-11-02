#ifndef MAILZ_CHARSET_H
#define MAILZ_CHARSET_H
#include <wchar.h>

#include "encoding.h"

enum charset_type {
	CHARSET_ASCII,
	CHARSET_OTHER,
	CHARSET_UTF8,
};

struct charset {
	enum charset_type type;

	union {
		struct charset_utf8 {
			mbstate_t mbs;
		} utf8;
	} v;
};

void charset_from_type(struct charset *, enum charset_type);
int charset_getc(struct charset *, struct encoding *, FILE *, char [static 4]);
#endif /* MAILZ_CHARSET_H */
