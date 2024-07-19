#include <sys/mman.h>
#include <sys/tree.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "argv.h"
#include "getline.h"
#include "header.h"
#include "utf8.h"

static int argv_map(struct argv_shm *, struct argv_mapped *);
static int argv_find(struct argv_mapped *, const char *);

#define ARGV_FOREACH(var, argv) for (char *(var) = (argv)->p; \
					((var) - (char *)(argv)->p) != (argv)->sz; \
					(var) += strlen((var)) + 1)

struct ignore {
	enum {
		IGNORE_ALL,
		IGNORE_NONE,
		IGNORE_IGNORE,
		IGNORE_RETAIN,
	} type;
	struct argv_mapped mapped;
};

struct header_rb {
	struct header header;
	RB_ENTRY(header_rb) entries;
};

static int header_rb_cmp(struct header_rb *, struct header_rb *);
RB_HEAD(headers, header_rb);
RB_PROTOTYPE_STATIC(headers, header_rb, entries, header_rb_cmp);
static int header_ignore(struct ignore *, const char *);

struct content_type {
	const char *type;
	const char *subtype;

	const char *charset;
};

static int content_type_parse(char *, struct content_type *);

static int is_allowed_ascii(int);

static int equal_escape(FILE *);

int
main(int argc, char *argv[])
{
	struct headers headers;
	struct header_rb f, *h, *i, *tv;
	struct getline gl;
	struct ignore ignore;
	struct argv_mapped reorder;
	struct utf8_decode u8;
	const char *errstr;
	FILE *fp;
	int ch, rv, qp, utf8;

	reorder.sz = 0;
	ignore.type = IGNORE_NONE;
	while ((ch = getopt(argc, argv, "ai:r:u:")) != -1) {
		switch (ch) {
		case 'a':
			ignore.type = IGNORE_ALL;
			break;
		case 'i':
			ignore.type = IGNORE_IGNORE;
			ignore.mapped.sz = strtonum(optarg, 0, LLONG_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "invalid usage");
			break;
		case 'r':
			reorder.sz = strtonum(optarg, 0, LLONG_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "invalid usage");
			break;
		case 'u':
			ignore.type = IGNORE_RETAIN;
			ignore.mapped.sz = strtonum(optarg, 0, LLONG_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "invalid usage");
			break;
		default:
			errx(1, "invalid usage");
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		errx(1, "invalid usage");

	if (ignore.type == IGNORE_IGNORE || ignore.type == IGNORE_RETAIN) {
		struct argv_shm shm;

		shm.fd = 3;
		shm.sz = ignore.mapped.sz;

		if (argv_map(&shm, &ignore.mapped) == -1)
			err(1, "mmap");
	}

	rv = 1;

	if (reorder.sz != 0) {
		struct argv_shm shm;

		shm.fd = 4;
		shm.sz = reorder.sz;

		if (argv_map(&shm, &reorder) == -1) {
			warn("mmap");
			goto ignore;
		}
	}

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
		warn("setlocale");
		goto reorder;
	}

	if ((fp = fopen(argv[0], "r")) == NULL) {
		warn("%s", argv[0]);
		goto reorder;
	}

	if (pledge("stdio", NULL) == -1) {
		warn("pledge");
		goto fp;
	}

	RB_INIT(&headers);
	memset(&gl, 0, sizeof(gl));
	for (;;) {
		struct header_rb *f, *header;

		if ((header = malloc(sizeof(*header))) == NULL) {
			warn("malloc");
			goto headers;
		}
		switch (header_read(fp, &gl, &header->header, 1)) {
		case HEADER_ERR:
			warnx("invalid header");
			free(header);
			goto headers;
		case HEADER_EOF:
			free(header);
			goto done;
		default:
			break;
		}

		if (strcasecmp(header->header.key, "content-type") != 0
				&& strcasecmp(header->header.key, "content-transfer-encoding") != 0
				&& header_ignore(&ignore, header->header.key)) {
			free(header->header.key);
			free(header->header.val);
			free(header);
			continue;
		}

		/* header of the same key already encountered, concatenate */
		if ((f = RB_INSERT(headers, &headers, header)) != NULL) {
			char *t;
			size_t clen, nlen, olen;

			olen = strlen(f->header.val);
			clen = strlen(header->header.val);
			nlen = olen + clen;

			if ((t = realloc(f->header.val, nlen + 1)) == NULL) {
				warn("realloc %zu", nlen + 1);
				free(header->header.key);
				free(header->header.val);
				free(header);
				goto headers;
			}
			f->header.val = t;
			memcpy(&f->header.val[olen], header->header.val, clen);
			f->header.val[nlen] = '\0';

			free(header->header.key);
			free(header->header.val);
			free(header);
		}
	}
	done:

	qp = 0;

	f.header.key = "content-transfer-encoding";
	if ((h = RB_FIND(headers, &headers, &f)) != NULL) {
		if (strcmp(h->header.val, "quoted-printable") == 0)
			qp = 1;
		if (header_ignore(&ignore, h->header.key)) {
			RB_REMOVE(headers, &headers, h);
			free(h->header.key);
			free(h->header.val);
			free(h);
		}
	}

	utf8 = 0;

	f.header.key = "content-type";
	if ((h = RB_FIND(headers, &headers, &f)) != NULL) {
		struct content_type ct;
		char *cts;

		if (header_ignore(&ignore, h->header.key)) {
			RB_REMOVE(headers, &headers, h);
			free(h->header.key);
			cts = h->header.val;
			free(h);
		}
		else {
			if ((cts = strdup(h->header.val)) == NULL) {
				warn("strdup");
				goto headers;
			}
		}
		if (content_type_parse(cts, &ct) != -1
				&& ct.charset != NULL
				&& strcasecmp(ct.charset, "utf-8") == 0)
			utf8 = 1;
		free(cts);
	}

	ARGV_FOREACH(s, &reorder) {
		f.header.key = s;

		if ((h = RB_FIND(headers, &headers, &f)) != NULL) {
			if (printf("%s: %s\n", h->header.key, h->header.val) < 0) {
				warn("printf");
				goto headers;
			}
			RB_REMOVE(headers, &headers, h);
			free(h->header.key);
			free(h->header.val);
			free(h);
		}
	}

	RB_FOREACH(i, headers, &headers) {
		if (printf("%s: %s\n", i->header.key, i->header.val) < 0) {
			warn("printf");
			goto headers;
		}
	}

	if (ignore.type != IGNORE_ALL && fputc('\n', stdout) == EOF) {
		warn("fputc");
		goto headers;
	}


	if (utf8) {
		memset(&u8, 0, sizeof(u8));
	}

	for (;;) {
		int c;

		if ((c = fgetc(fp)) == EOF) {
			if (utf8 && u8.n != 0) {
				/* EOF in the middle of decoding a utf-8 codepoint */
				warnx("letter had invalid utf-8");
				goto headers;
			}

			break;
		}

		if (c == '=' && qp) {
			switch (c = equal_escape(fp)) {
			case -2:
				/* soft break */
				continue;
			case EOF:
				warnx("letter had invalid ascii");
				goto headers;
			default:
				break;
			}
		}

		if (utf8) {
			switch (utf8_decode(&u8, c)) {
			case UTF8_DECODE_DONE:
				if (u8.n == 1 
						&& !is_allowed_ascii((unsigned char)u8.buf[0])) {
					u8.n = 0;
					goto invalid;
				}
				if (fwrite(u8.buf, u8.n, 1, stdout) != 1) {
					warn("write");
					goto headers;
				}
				u8.n = 0;
				break;
			case UTF8_DECODE_MORE:
				continue;
			case UTF8_DECODE_INVALID:
				rv = 1;
				goto headers;
			}
		}
		else {
			if (!is_allowed_ascii((unsigned char)c)) {
				goto invalid;
			}
			if (fputc(c, stdout) == EOF) {
				warn("fputc");
				goto headers;
			}
		}

		continue;

		invalid:

		/* unicode replacement character */
		if (fwrite("\xEF\xBF\xBD", 3, 1, stdout) != 1) {
			warn("write");
			goto headers;
		}
		if (utf8)
			memset(&u8, 0, sizeof(u8));
	}

	rv = 0;
	headers:
	RB_FOREACH_SAFE(i, headers, &headers, tv) {
		RB_REMOVE(headers, &headers, i);
		free(i->header.key);
		free(i->header.val);
		free(i);
	}
	free(gl.line);
	fp:
	if (fclose(fp) == EOF) {
		warn("fclose");
		rv = 1;
	}
	reorder:
	if (reorder.sz != 0)
		(void) munmap(reorder.p, reorder.sz);
	ignore:
	if (ignore.type == IGNORE_IGNORE || ignore.type == IGNORE_RETAIN)
		(void) munmap(ignore.mapped.p, ignore.mapped.sz);
	return rv;
}

RB_GENERATE_STATIC(headers, header_rb, entries, header_rb_cmp);

static int
header_rb_cmp(struct header_rb *one, struct header_rb *two)
{
	return strcasecmp(one->header.key, two->header.key);
}

static int
content_type_parse(char *s, struct content_type *out)
{
	char *charset, *tst, *param, *subtype, *type;

	if ((tst = strsep(&s, ";")) == NULL)
		return -1;
	if ((type = strsep(&tst, "/")) == NULL)
		return -1;
	if ((subtype = tst) == NULL)
		return -1;

	charset = NULL;
	while ((param = strsep(&s, ";")) != NULL) {
		char *key, *val;
		size_t len;

		if ((key = strsep(&param, "=")) == NULL)
			return -1;
		if ((val = param) == NULL)
			return -1;

		if (*key == ' ')
			key++;

		len = strlen(val);
		if (val[0] == '\"' && val[len - 1] == '\"') {
			val[len - 1] = '\0';
			val++;
		}
		if (!strcasecmp(key, "charset"))
			charset = val;
	}

	out->charset = charset;
	out->subtype = subtype;
	out->type = type;
	return 0;
}

static int
is_allowed_ascii(int c)
{
	return isprint(c) || isspace(c);
}

static int
hexdigcaps(int c)
{
	if (isdigit(c))
		return c - '0';
	if (isxdigit(c) && isupper(c))
		return (c - 'A') + 10;
	return -1;
}

static int
equal_escape(FILE *fp)
{
	int t, d;

	if ((t = fgetc(fp)) == EOF)
		return '=';
	if (t == '\n')
		return -2;

	if ((d = fgetc(fp)) == EOF) {
		(void) fseek(fp, -1, SEEK_CUR);
		return EOF;
	}

	if ((t = hexdigcaps(t)) == -1 || (d = hexdigcaps(d)) == -1) {
		(void) fseek(fp, -1, SEEK_CUR);
		return EOF;
	}

	return (t << 4) | d;
}

static int
header_ignore(struct ignore *ignore, const char *key)
{
	switch (ignore->type) {
	case IGNORE_ALL:
		return 1;
	case IGNORE_NONE:
		return 0;
	case IGNORE_IGNORE:
		return argv_find(&ignore->mapped, key);
	case IGNORE_RETAIN:
		return !argv_find(&ignore->mapped, key);
	}
}

static int
argv_map(struct argv_shm *shm, struct argv_mapped *mapped)
{
	void *p;

	p = mmap(NULL, shm->sz, PROT_READ, MAP_PRIVATE, shm->fd, 0);
	if (p == MAP_FAILED)
		return -1;
	mapped->p = p;
	mapped->sz = shm->sz;
	return 0;
}

static int
argv_find(struct argv_mapped *mapped, const char *key)
{
	ARGV_FOREACH(i, mapped) {
		if (strcasecmp(i, key) == 0)
			return 1;
	}
	return 0;
}
