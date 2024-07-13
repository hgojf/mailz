#ifndef MAILZ_UTF8_H
#define MAILZ_UTF8_H
struct utf8_decode {
	mbstate_t mbs;
	int n;
	char buf[4];
};

enum {
	UTF8_DECODE_DONE,
	UTF8_DECODE_MORE,
	UTF8_DECODE_INVALID,
};

int utf8_decode(struct utf8_decode *, char);
#endif /* MAILZ_UTF8_H */
