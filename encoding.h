#ifndef MAILZ_ENCODING_H
#define MAILZ_ENCODING_H
#include <bitstring.h>

struct encoding_qp {
	#define QP_WS_MAX 73
	bitstr_t bit_decl(space, QP_WS_MAX);
	int nspace;
	int off;
};

enum encoding_type {
	ENCODING_BINARY,
	ENCODING_7BIT,
	ENCODING_8BIT,
	ENCODING_QP,
};

struct encoding {
	enum encoding_type type;

	struct encoding_qp qp;
};

#define ENCODING_EOF -129
#define ENCODING_ERR -130

enum encoding_type encoding_from_name(const char *);
int encoding_getc(struct encoding *, FILE *);
void encoding_set(struct encoding *, enum encoding_type);
int encoding_init(struct encoding *);
#endif /* MAILZ_ENCODING_H */
