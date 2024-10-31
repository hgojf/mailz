#include <bitstring.h>
#include <stdio.h>
#include <string.h>

#include "encoding.h"

#define nitems(a) (sizeof(a) / sizeof(*(a)))

static int encoding_qp(struct encoding_qp *, FILE *);
static int hexdigcaps(int);

struct {
	const char *ident;
	enum encoding_type type;
} encodings[] = {
	{ "binary", ENCODING_BINARY },
	{ "7bit", ENCODING_7BIT },
	{ "8bit", ENCODING_8BIT },
	{ "quoted-printable", ENCODING_QP },
};

enum encoding_type
encoding_from_name(const char *s)
{
	size_t i;

	for (i = 0; i < nitems(encodings); i++)
		if (!strcmp(encodings[i].ident, s))
			return encodings[i].type;
	return -1;
}

int
encoding_getc(struct encoding *e, FILE *fp)
{
	int ch;

	switch (e->type) {
	case ENCODING_BINARY:
	case ENCODING_7BIT:
	case ENCODING_8BIT:
		if ((ch = fgetc(fp)) == EOF)
			return ENCODING_EOF;
		if (e->type != ENCODING_BINARY && ch == '\0')
			return ENCODING_ERR;
		if (e->type == ENCODING_7BIT && ch > 127)
			return ENCODING_ERR;
		return ch;
	case ENCODING_QP:
		return encoding_qp(&e->qp, fp);
	default:
		return ENCODING_ERR;
	}
}

int
encoding_init(struct encoding *e)
{
	if (e->type == 0)
		return 0;
	return 1;
}

static int
encoding_qp(struct encoding_qp *qp, FILE *fp)
{
	for (;;) {
		int ch, hi, lo;

		if ((ch = fgetc(fp)) == EOF)
			return ENCODING_EOF;

		#if 0
		if (ch == ' ' || ch == '\t') {
			if (qp->nspace == QP_WS_MAX)
				return ENCODING_ERR;
			if (ch == ' ')
				bit_set(qp->space, qp->nspace++);
			else
				bit_clear(qp->space, qp->nspace++);
			qp->off = 0;
			continue;
		}
		else if (ch == '\n') {
			qp->nspace = 0;
			qp->off = 0;
		}
		else if (qp->nspace != qp->off) {
			int rv;

			if (bit_test(qp->space, qp->off))
				rv = ' ';
			else
				rv = '\t';

			qp->off++;
			return rv;
		}
		#endif

		if (ch != '=')
			return ch;

		if ((hi = fgetc(fp)) == EOF)
			return ENCODING_ERR;
		if (hi == '\n')
			continue;
		if ((hi = hexdigcaps(hi)) == -1)
			return ENCODING_ERR;

		if ((lo = fgetc(fp)) == EOF)
			return ENCODING_ERR;
		if ((lo = hexdigcaps(lo)) == -1)
			return ENCODING_ERR;

		return (hi << 4) | lo;
	}
}

void
encoding_set(struct encoding *e, enum encoding_type type)
{
	switch (type) {
	case ENCODING_BINARY:
	case ENCODING_7BIT:
	case ENCODING_8BIT:
		break;
	case ENCODING_QP:
		memset(&e->qp, 0, sizeof(e->qp));
		break;
	}

	e->type = type;
}


static int
hexdigcaps(int ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	return -1;
}
