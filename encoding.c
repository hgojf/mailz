#include <stdio.h>
#include <string.h>

#include "encoding.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

static int encoding_qp(struct encoding_qp *, FILE *);
static int encoding_raw(FILE *, int);
static int hexdigcaps(int);

#define ENC_RAW_7BIT 0x1
#define ENC_RAW_NONUL 0x2

struct {
	const char *ident;
	enum encoding_type type;
} encodings[] = {
	{ "7bit",		ENCODING_7BIT },
	{ "8bit",		ENCODING_8BIT },
	{ "binary",		ENCODING_BINARY },
	{ "quoted-printable",	ENCODING_QP },
};

int
encoding_from_name(struct encoding *e, const char *name)
{
	size_t i;

	for (i = 0; i < nitems(encodings); i++) {
		if (!strcmp(name, encodings[i].ident)) {
			encoding_from_type(e, encodings[i].type);
			return 0;
		}
	}

	return -1;
}

void
encoding_from_type(struct encoding *e, enum encoding_type type)
{
	switch (type) {
	case ENCODING_7BIT:
	case ENCODING_8BIT:
	case ENCODING_BINARY:
		break;
	case ENCODING_QP:
		memset(&e->qp, 0, sizeof(e->qp));
		break;
	}

	e->type = type;
}

int
encoding_getc(struct encoding *e, FILE *fp)
{
	switch (e->type) {
	case ENCODING_7BIT:
		return encoding_raw(fp, ENC_RAW_NONUL | ENC_RAW_7BIT);
	case ENCODING_8BIT:
		return encoding_raw(fp, ENC_RAW_NONUL);
	case ENCODING_BINARY:
		return encoding_raw(fp, 0);
	case ENCODING_QP:
		return encoding_qp(&e->qp, fp);
	}

	return ENCODING_ERR;
}

static int
encoding_qp(struct encoding_qp *qp, FILE *fp)
{
	for (;;) {
		int ch, hi, lo;

		if ((ch = fgetc(fp)) == EOF)
			return ENCODING_EOF;

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

static int
encoding_raw(FILE *fp, int flags)
{
	int ch;

	if ((ch = fgetc(fp)) == EOF)
		return ENCODING_EOF;

	if (flags & ENC_RAW_NONUL && ch == '\0')
		return ENCODING_ERR;
	if (flags & ENC_RAW_7BIT && ch > 127)
		return ENCODING_ERR;

	return ch;
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
