#ifndef MAILZ_ENCODING_H
#define MAILZ_ENCODING_H
struct encoding {
	enum encoding_type {
		ENCODING_7BIT,
		ENCODING_8BIT,
		ENCODING_BASE64,
		ENCODING_BINARY,
		ENCODING_QUOTED_PRINTABLE,
	} type;

	union {
		struct b64_decode {
			char buf[2]; /* extra chars */
			int i; /* what idx of buf has a char, or end if none */
			int end;
		} base64;
	} val;
};

/* out of range for a signed char */
#define ENCODING_EOF -129
#define ENCODING_ERR -130

int encoding_getbyte(FILE *, struct encoding *);
int encoding_set(struct encoding *, const char *);
void encoding_set_type(struct encoding *, enum encoding_type);
#endif /* MAILZ_ENCODING_H */
