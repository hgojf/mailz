#ifndef MAILZ_ENCODING_H
#define MAILZ_ENCODING_H
enum encoding_type {
	ENCODING_7BIT,
	ENCODING_8BIT,
	ENCODING_BINARY,
	ENCODING_QP,
};

struct encoding {
	struct encoding_qp {
	} qp;

	enum encoding_type type;
};

#define ENCODING_EOF -129
#define ENCODING_ERR -130

int encoding_from_name(struct encoding *, const char *);
void encoding_from_type(struct encoding *, enum encoding_type);
int encoding_getc(struct encoding *, FILE *);
#endif /* MAILZ_ENCODING_H */
