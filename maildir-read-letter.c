#include <sys/mman.h>
#include <sys/tree.h>

#include <ctype.h>
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
#include "maildir-read-letter.h"
#include "utf8.h"

static int argv_map(struct argv_shm *, struct argv_mapped *);
static int argv_find(struct argv_mapped *, const char *);

struct ignore {
	enum {
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

static int equal_escape(FILE *, int);

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
	ssize_t nw;
	int ch, rv, save_errno, qp, utf8;

	reorder.sz = 0;
	ignore.type = IGNORE_NONE;
	while ((ch = getopt(argc, argv, "i:r:u:")) != -1) {
		switch (ch) {
		case 'i':
			ignore.type = IGNORE_IGNORE;
			ignore.mapped.sz = strtonum(optarg, 0, LLONG_MAX, &errstr);
			if (errstr != NULL) {
				save_errno = 0;
				rv = MAILDIR_READ_LETTER_USAGE;
				goto fail;
			}
			break;
		case 'r':
			reorder.sz = strtonum(optarg, 0, LLONG_MAX, &errstr);
			if (errstr != NULL) {
				save_errno = 0;
				rv = MAILDIR_READ_LETTER_USAGE;
				goto fail;
			}
			break;
		case 'u':
			ignore.type = IGNORE_RETAIN;
			ignore.mapped.sz = strtonum(optarg, 0, LLONG_MAX, &errstr);
			if (errstr != NULL) {
				save_errno = 0;
				rv = MAILDIR_READ_LETTER_USAGE;
				goto fail;
			}
			break;
		default:
			save_errno = 0;
			rv = MAILDIR_READ_LETTER_USAGE;
			goto fail;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		save_errno = 0;
		rv = MAILDIR_READ_LETTER_USAGE;
		goto fail;
	}

	if (ignore.type != IGNORE_NONE) {
		struct argv_shm shm;

		shm.fd = 4;
		shm.sz = ignore.mapped.sz;

		if (argv_map(&shm, &ignore.mapped) == -1) {
			save_errno = errno;
			rv = MAILDIR_READ_LETTER_SHM;
			goto fail;
		}
	}

	if (reorder.sz != 0) {
		struct argv_shm shm;

		shm.fd = 5;
		shm.sz = ignore.mapped.sz;

		if (argv_map(&shm, &reorder) == -1) {
			save_errno = errno;
			rv = MAILDIR_READ_LETTER_SHM;
			goto ignore;
		}
	}

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
		save_errno = 0;
		rv = MAILDIR_READ_LETTER_SETLOCALE;
		goto reorder;
	}

	if ((fp = fopen(argv[0], "r")) == NULL) {
		save_errno = errno;
		rv = MAILDIR_READ_LETTER_FOPEN;
		goto fail;
	}

	if (pledge("stdio", NULL) == -1) {
		save_errno = errno;
		rv = MAILDIR_READ_LETTER_PLEDGE;
		goto fp;
	}

	RB_INIT(&headers);
	memset(&gl, 0, sizeof(gl));
	for (;;) {
		struct header_rb *f, *header;

		if ((header = malloc(sizeof(*header))) == NULL) {
			save_errno = errno;
			rv = MAILDIR_READ_LETTER_MALLOC;
			goto headers;
		}
		switch (header_read(fp, &gl, &header->header, 0)) {
		case HEADER_ERR:
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
				save_errno = errno;
				rv = MAILDIR_READ_LETTER_MALLOC;
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
			if ((cts = strdup(h->header.val)) == NULL)
				goto headers;
		}
		if (content_type_parse(cts, &ct) != -1
				&& ct.charset != NULL
				&& strcasecmp(ct.charset, "utf-8") == 0)
			utf8 = 1;
		free(cts);
	}

	RB_FOREACH_SAFE(i, headers, &headers, tv) {
		if (!argv_find(&reorder, i->header.key))
			continue;
		if (printf("%s: %s\n", i->header.key, i->header.val) < 0) {
			save_errno = errno;
			rv = MAILDIR_READ_LETTER_PRINTF;
			goto headers;
		}
		RB_REMOVE(headers, &headers, i);
		free(i->header.key);
		free(i->header.val);
		free(i);
	}

	RB_FOREACH(i, headers, &headers) {
		if (printf("%s: %s\n", i->header.key, i->header.val) < 0) {
			save_errno = errno;
			rv = MAILDIR_READ_LETTER_PRINTF;
			goto headers;
		}
	}

	if (fputc('\n', stdout) == EOF) {
		save_errno = errno;
		rv = MAILDIR_READ_LETTER_PRINTF;
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
				save_errno = 0;
				rv = MAILDIR_READ_LETTER_UTF8;
				goto headers;
			}

			break;
		}

		if (c == '=') {
			switch (c = equal_escape(fp, qp)) {
			case EOF:
				save_errno = 0;
				rv = MAILDIR_READ_LETTER_ASCII;
				goto headers;
			case -2:
				/* soft break */
				continue;
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
					save_errno = errno;
					rv = MAILDIR_READ_LETTER_PRINTF;
					goto headers;
				}
				u8.n = 0;
				break;
			case UTF8_DECODE_MORE:
				continue;
			case UTF8_DECODE_INVALID:
				save_errno = 0;
				rv = MAILDIR_READ_LETTER_ASCII;
				goto headers;
			}
		}
		else {
			if (!is_allowed_ascii((unsigned char)c)) {
				goto invalid;
			}
			if (fputc(c, stdout) == EOF) {
				save_errno = errno;
				rv = MAILDIR_READ_LETTER_PRINTF;
				goto headers;
			}
		}

		continue;

		invalid:

		/* unicode replacement character */
		if (fwrite("\xEF\xBF\xBD", 3, 1, stdout) != 1) {
			save_errno = errno;
			rv = MAILDIR_READ_LETTER_PRINTF;
			goto headers;
		}
		if (utf8)
			memset(&u8, 0, sizeof(u8));
	}

	save_errno = 0;
	rv = MAILDIR_READ_LETTER_OK;
	headers:
	RB_FOREACH_SAFE(i, headers, &headers, tv) {
		RB_REMOVE(headers, &headers, i);
		free(i->header.key);
		free(i->header.val);
		free(i);
	}
	free(gl.line);
	fp:
	if (fclose(fp) == EOF && rv == MAILDIR_READ_LETTER_OK) {
		save_errno = errno;
		rv = MAILDIR_READ_LETTER_CLOSE;
	}
	reorder:
	if (reorder.sz != 0)
		(void) munmap(reorder.p, reorder.sz);
	ignore:
	if (ignore.type != IGNORE_IGNORE)
		(void) munmap(ignore.mapped.p, ignore.mapped.sz);
	fail:
	nw = write(STDERR_FILENO, &save_errno, sizeof(save_errno));
	if (nw == -1 && rv == MAILDIR_READ_LETTER_OK) {
		save_errno = errno;
		rv = MAILDIR_READ_LETTER_WRITE;
	}
	else if (nw != sizeof(save_errno) && rv == MAILDIR_READ_LETTER_OK)
		rv = MAILDIR_READ_LETTER_SWRITE;
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
equal_escape(FILE *fp, int qp)
{
	int t;

	if ((t = fgetc(fp)) == EOF)
		return '=';
	if (t == '\n')
		return -2;

	if (qp) {
		int d;

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
	else {
		if (fseek(fp, -1, SEEK_CUR) == -1)
			return EOF;
		return '=';
	}
}

static int
header_ignore(struct ignore *ignore, const char *key)
{
	if (ignore->type == IGNORE_IGNORE) {
		return argv_find(&ignore->mapped, key);
	}
	else if (ignore->type == IGNORE_RETAIN)
		return !argv_find(&ignore->mapped, key);
	else
		return 0;
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
	void *h;
	size_t ll;

	if (mapped->sz == 0)
		return 0;

	ll = strlen(key);

	h = mapped->p;
	while ((h = memmem(h, mapped->sz - (h - mapped->p), key, ll)) != NULL) {
		if ((h == mapped->p || ((char *)h)[-1] == '\0') && ((char *)h)[ll] == '\0')
			return 1;
	}
	return 0;
}
