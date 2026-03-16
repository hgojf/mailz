#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "charset.h"
#include "encoding.h"
#include "header.h"
#include "stdio.h"
#include "multipart.h"

struct multipart_content_type {
	char *type;
	size_t typesz;

	char *subtype;
	size_t subtypesz;

	int charset;

	char *boundary;
	size_t boundarysz;

	char *name;
	size_t namesz;
};

struct part_internal {
	off_t start;
	off_t end;

	int charset;
	int encoding;

	char boundary[MULTIPART_BOUNDARY_MAX];
};

#define static_assert(what, msg)

static int read_part_headers(FILE *, struct part_internal *, struct part *,
			     FILE *, int (*) (const char *, void *), void *);
static int skip_part_body(FILE *, const char *, off_t *, int);

int
multipart_choose(struct multipart *mp, size_t choice)
{
	struct part_internal *part, *parts;
	off_t start;

	if (mp->state != MULTIPART_STATE_CHOOSE)
		return -1;

	if (choice >= mp->data.choose.npart)
		return -1;

	part = &mp->data.choose.parts[choice];

	parts = mp->data.choose.parts;
	start = part->start;
	if (part->boundary[0] != '\0') {
		static_assert(sizeof(mp->data.parts.boundary) ==
			      sizeof(part->boundary), "");
		memcpy(mp->data.parts.boundary, part->boundary,
		       sizeof(mp->data.parts.boundary));
		mp->data.parts.npart = 0;
		mp->data.parts.parts = NULL;
		mp->state = MULTIPART_STATE_PARTS;
	}
	else {
		charset_from_type(&mp->data.body.charset, part->charset);
		encoding_from_type(&mp->data.body.encoding, part->encoding);
		mp->data.body.encoding.left = part->end - part->start;
		mp->state = MULTIPART_STATE_BODY;
	}
	free(parts);

	if (fseeko(mp->fp, start, SEEK_SET) == -1)
		return -1;
	return 0;
}

static int
multipart_content_type(FILE *fp, FILE *echo,
		       struct multipart_content_type *type)
{
	#define MULTIPART_TYPE_TEXT 0
	#define MULTIPART_TYPE_MULTIPART 1
	#define MULTIPART_TYPE_OTHER 2
	struct content_type ctype;
	int charset, eof, got_boundary, kind;

	ctype.type = type->type;
	ctype.typesz = type->typesz;
	ctype.subtype = type->subtype;
	ctype.subtypesz = type->subtypesz;

	eof = 0;
	if (header_content_type(fp, echo, &ctype, &eof) < 0)
		return -1;

	if (!ctype.type_trunc && !strcasecmp(ctype.type, "multipart"))
		kind = MULTIPART_TYPE_MULTIPART;
	else if (!ctype.type_trunc && !strcasecmp(ctype.type, "text"))
		kind = MULTIPART_TYPE_TEXT;
	else
		kind = MULTIPART_TYPE_OTHER;

	charset = CHARSET_UNKNOWN;
	if (type->namesz != 0)
		type->name[0] = '\0';
	got_boundary = 0;
	for (;;) {
		struct content_type_var cvar;
		int error;
		char val[MULTIPART_BOUNDARY_MAX], var[9];

		cvar.var = var;
		cvar.varsz = sizeof(var);
		cvar.val = val;
		cvar.valsz = sizeof(val);

		error = header_content_type_var(fp, echo, &cvar, &eof);
		if (error == HEADER_EOF)
			break;
		if (error < 0)
			return -1;

		if (kind == MULTIPART_TYPE_MULTIPART && !cvar.var_trunc &&
		    !strcasecmp(var, "boundary")) {
			if (cvar.val_trunc)
				return -1;
			if (strlcpy(type->boundary, val, type->boundarysz)
				    >= type->boundarysz)
				return -1;
			got_boundary = 1;
		}
		else if (kind == MULTIPART_TYPE_TEXT && !cvar.var_trunc &&
			 !strcasecmp(var, "charset")) {
			if (cvar.val_trunc ||
			    (charset = charset_from_name(val)) == CHARSET_UNKNOWN)
				charset = CHARSET_OTHER;
		}
		else if (!cvar.var_trunc && !strcasecmp(var, "name")) {
			if (!cvar.val_trunc && type->namesz != 0)
				strlcpy(type->name, val, type->namesz);
		}
	}

	if (!got_boundary)
		type->boundary = NULL;
	type->charset = charset;

	return 0;

	#undef MULTIPART_TYPE_TEXT
	#undef MULTIPART_TYPE_MULTIPART
	#undef MULTIPART_TYPE_OTHER
}

void
multipart_free(struct multipart *mp)
{
	switch (mp->state) {
	case MULTIPART_STATE_PARTS:
		free(mp->data.parts.parts);
		break;
	case MULTIPART_STATE_CHOOSE:
		free(mp->data.choose.parts);
		break;
	}
}

int
multipart_getc(struct multipart *mp, char buf[static 4])
{
	if (mp->state != MULTIPART_STATE_BODY)
		return -1;
	return charset_getc(&mp->data.body.charset, &mp->data.body.encoding,
			    mp->fp, buf);
}

int
multipart_init(struct multipart *mp, FILE *fp, FILE *recho,
	       int (*ignore) (const char *, void *), void *ignore_cookie)
{
	int charset, encoding;
	char boundary[MULTIPART_BOUNDARY_MAX];

	charset = CHARSET_UNKNOWN;
	boundary[0] = '\0';
	encoding = ENCODING_UNKNOWN;

	for (;;) {
		FILE *echo;
		int error;
		char buf[HEADER_NAME_LEN];

		if ((error = header_name(fp, buf, sizeof(buf))) == HEADER_EOF)
			break;
		if (error < 0)
			return -1;

		echo = NULL;
		if (ignore != NULL && !ignore(buf, ignore_cookie))
			echo = recho;

		if (echo)
			if (fprintf(echo, "%s:", buf) < 0)
				return -1;

		if (!strcasecmp(buf, "content-transfer-encoding")) {
			char buf[17];

			if (header_encoding(fp, echo, buf, sizeof(buf)) < 0)
				return -1;
			if ((encoding = encoding_from_name(buf)) ==
			    ENCODING_UNKNOWN)
				return -1;
		}
		else if (!strcasecmp(buf, "content-type")) {
			struct multipart_content_type type;
			char buf[10];

			type.type = buf;
			type.typesz = sizeof(buf);

			type.subtype = NULL;
			type.subtypesz = 0;

			type.name = NULL;
			type.namesz = 0;

			type.boundary = boundary;
			type.boundarysz = sizeof(boundary);
			if (multipart_content_type(fp, echo, &type) == -1)
				return -1;
			/*
			 * If the Content-Type was a multipart type then
			 * boundary has been set to a non empty string.
			 */

			if (type.boundary == NULL && type.charset != CHARSET_UNKNOWN)
				charset = type.charset;
		}
		else {
			if (header_skip(fp, echo) < 0)
				return -1;
		}
	}

	if (boundary[0] != '\0') {
		/*
		 * This is a multipart message, choose parts until we
		 * have chosen a part that is not of a multipart type.
		 */
		static_assert(sizeof(mp->data.parts.boundary) == sizeof(boundary),
			      "boundary buffer size not MULTIPART_BOUNDARY_MAX");
		memcpy(mp->data.parts.boundary, boundary,
		       sizeof(mp->data.parts.boundary));
		mp->data.parts.npart = 0;
		mp->data.parts.parts = NULL;
		mp->state = MULTIPART_STATE_PARTS;
	}
	else {
		/*
		 * This is not a multipart message, we can skip the
		 * whole part selection process and just read the body.
		 */
		if (charset == CHARSET_UNKNOWN)
			charset = CHARSET_ASCII;
		if (encoding == ENCODING_UNKNOWN)
			encoding = ENCODING_7BIT;

		charset_from_type(&mp->data.body.charset, charset);
		encoding_from_type(&mp->data.body.encoding, encoding);
		mp->state = MULTIPART_STATE_BODY;
	}

	mp->fp = fp;

	return 0;
}

int
multipart_next(struct multipart *mp, struct part *part)
{
	struct part_internal *t, ipart;
	int done, error;

	if (mp->state == MULTIPART_STATE_CHOOSE)
		return 0;
	if (mp->state == MULTIPART_STATE_BODY)
		return 0;

	/*
	 * Skip the preamble before the first part.
	 */
	if (mp->data.parts.npart == 0)
		if ((error = skip_part_body(mp->fp, mp->data.parts.boundary,
					    NULL, 1)) == -1)
			return -1;

	if (read_part_headers(mp->fp, &ipart, part, NULL, NULL, NULL) == -1)
		return -1;

	if ((ipart.start = ftello(mp->fp)) == -1)
		return -1;

	if ((error = skip_part_body(mp->fp, mp->data.parts.boundary,
				    &ipart.end, 0)) == -1)
		return -1;
	done = (error == 0);

	t = reallocarray(mp->data.parts.parts, mp->data.parts.npart + 1,
			 sizeof(*mp->data.parts.parts));
	if (t == NULL)
		return -1;

	mp->data.parts.parts = t;
	mp->data.parts.parts[mp->data.parts.npart++] = ipart;

	if (done) {
		struct part_internal *parts;
		size_t npart;

		npart = mp->data.parts.npart;
		parts = mp->data.parts.parts;

		mp->data.choose.npart = npart;
		mp->data.choose.parts = parts;
		mp->state = MULTIPART_STATE_CHOOSE;
	}

	return 1;
}

size_t
multipart_npart(struct multipart *mp)
{
	switch (mp->state) {
	case MULTIPART_STATE_CHOOSE:
		return mp->data.choose.npart;
	default: /* Maybe be stricter here? */
		return 0;
	}
}

static int
read_part_headers(FILE *fp, struct part_internal *ipart, struct part *part,
		  FILE *recho, int (*ignore) (const char *, void *),
		  void *ignore_cookie)
{
	int charset, encoding;

	charset = CHARSET_UNKNOWN;
	ipart->boundary[0] = '\0';

	part->name[0] = '\0';

	strlcpy(part->type, "plain", sizeof(part->type));
	strlcpy(part->subtype, "text", sizeof(part->subtype));

	encoding = ENCODING_7BIT;
	for (;;) {
		FILE *echo;
		int error;
		char buf[HEADER_NAME_LEN];

		if ((error = header_name(fp, buf, sizeof(buf))) == HEADER_EOF)
			break;
		if (error < 0)
			return -1;

		echo = NULL;
		if (ignore != NULL && ignore(buf, ignore_cookie))
			echo = recho;

		if (echo)
			if (fprintf(echo, "%s:", buf) < 0)
				return -1;

		if (!strcasecmp(buf, "content-transfer-encoding")) {
			char buf[17];

			if (header_encoding(fp, echo, buf, sizeof(buf)) < 0)
				return -1;
			if ((encoding = encoding_from_name(buf)) ==
			    ENCODING_UNKNOWN)
				return -1;
		}
		else if (!strcasecmp(buf, "content-type")) {
			struct multipart_content_type type;

			type.type = part->type;
			type.typesz = sizeof(part->type);

			type.subtype = part->subtype;
			type.subtypesz = sizeof(part->subtype);

			type.name = part->name;
			type.namesz = sizeof(part->name);

			type.boundary = ipart->boundary;
			type.boundarysz = sizeof(ipart->boundary);

			if (multipart_content_type(fp, echo, &type) == -1)
				return -1;
			/*
			 * A the Content-Type was a multipart type then
			 * boundary has been set to a non empty string.
			 */

			if (type.boundary == NULL && type.charset != CHARSET_UNKNOWN)
				charset = type.charset;
		}
		else {
			if (header_skip(fp, echo) < 0)
				return -1;
		}
	}

	if (charset == CHARSET_UNKNOWN)
		charset = CHARSET_OTHER;

	ipart->charset = charset;
	ipart->encoding = encoding;
	return 0;
}

static int
skip_part_body(FILE *fp, const char *boundary, off_t *o_end, int preamble)
{
	int first;

	first = 1;
	for (;;) {
		off_t end;
		int ch;
		const char *b;

		/*
		 * On OpenBSD, libc tries to use the file offset stored
		 * in the file structure instead of calling lseek(2),
		 * unless it encountered a read error. Thus, this should
		 * be cheap.
		 */
		if ((end = ftello(fp)) == EOF)
			return -1;

		if (!first) {
			first = 0;
			if ((ch = fgetc(fp)) == EOF)
				return -1;
			if (0) {
				again:
				if ((end = ftello(fp)) == EOF)
					return -1;
				/*
				 * end refers to the byte just before the
				 * newline which begins the boundary.
				 */
				end -= 1;
			}
			if (ch != '\n')
				continue;
		}

		if ((ch = fgetc(fp)) == EOF)
			return -1;
		if (ch != '-' || (ch = fgetc(fp)) != '-')
			goto again;

		for (b = boundary; *b != '\0'; b++)
			if ((ch = fgetc(fp)) != *b)
				goto again;

		for (;;) {
			if ((ch = fgetc(fp)) == EOF)
				return -1;
			if (ch != ' ' && ch != '\t')
				break;
		}

		if (ch == '\n') {
			if (o_end != NULL)
				*o_end = end;
			return 1;
		}

		/*
		 * This is actually the preamble before the first part,
		 * so the boundary cant be followed by '--'.
		 */
		if (preamble)
			goto again;

		if (ch != '-' || (ch = fgetc(fp)) != '-')
			goto again;
		if ((ch = fgetc(fp)) != EOF && ch != '\n')
			goto again;
		if (o_end != NULL)
			*o_end = end;
		return 0;
	}
}
