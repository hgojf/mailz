#ifndef MAILZ_ENCODING_H
#define MAILZ_ENCODING_H
enum encoding_type {
	ENCODING_7BIT,
	ENCODING_8BIT,
	ENCODING_BASE64,
	ENCODING_BINARY,
	ENCODING_QP,
};

struct encoding {
	union {
		struct encoding_b64 {
			char buf[2];
			int start;
			int end;
		} b64;
		struct encoding_qp {
		} qp;
	} v;

	enum encoding_type type;
};

#define ENCODING_EOF -129
#define ENCODING_ERR -130

int encoding_from_name(struct encoding *, const char *);
void encoding_from_type(struct encoding *, enum encoding_type);
int encoding_getc(struct encoding *, FILE *);
#endif /* MAILZ_ENCODING_H */
