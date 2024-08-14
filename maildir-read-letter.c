#include <sys/queue.h>
#include <sys/tree.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <imsg.h>
#include <locale.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wchar.h>

#include "header.h"
#include "imsg-util.h"
#include "maildir-read-letter.h"

enum charset {
	CHARSET_ASCII,
	CHARSET_OTHER,
	CHARSET_UTF8,
};

struct content_type {
	char *charset;
	char *subtype;
	char *type;
};

enum encoding {
	ENCODING_PLAIN,
	ENCODING_QP,
};

struct ignore {
	size_t argc;
	char **argv;
	enum {
		IGNORE_IGNORE = 0,
		IGNORE_ALL,
		IGNORE_RETAIN,
	} type;
};

struct reorder {
	size_t argc;
	char **argv;
};

struct header_rb {
	char *key;
	char *val;
	RB_ENTRY(header_rb) entries;
};

RB_HEAD(headers, header_rb);

#define GB_ERR -2
#define GB_EOF -1

static int content_type_parse(char *, struct content_type *);
static int getbyte(enum encoding);
static int get_char(enum charset, enum encoding, mbstate_t *, 
	char [static 4]);
static int header_ignore(const char *, struct ignore *);
static int header_rb_cmp(struct header_rb *, struct header_rb *);
RB_PROTOTYPE_STATIC(headers, header_rb, entries, header_rb_cmp);
static int hexdigcaps(int);
static int recv_argv(struct imsg *, size_t *, char ***);

int
main(int argc, char *argv[])
{
	enum charset charset;
	enum encoding encoding;
	struct getline gl;
	struct headers headers;
	struct header_rb find, *found, *h, *h2;
	struct imsgbuf msgbuf;
	struct ignore ignore;
	mbstate_t mbs;
	struct reorder reorder;
	int rv;

	if (argc != 1)
		errx(1, "invalid usage");

	rv = 1;

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
		warn("setlocale");
		goto fd;
	}

	/* setlocale needs rpath */
	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	imsg_init(&msgbuf, 3);
	memset(&ignore, 0, sizeof(ignore));
	memset(&reorder, 0, sizeof(reorder));

	for (;;) {
		struct imsg msg;
		ssize_t n;

		if ((n = imsg_get_blocking(&msgbuf, &msg)) == -1) {
			warnx("imsg_get_blocking");
			goto msgbuf;
		}
		if (n == 0)
			break;

		switch (imsg_get_type(&msg)) {
		case IMSG_MDR_IGNOREALL:
			ignore.type = IGNORE_ALL;
			break;
		case IMSG_MDR_IGNORE:
		case IMSG_MDR_RETAIN:
			if (ignore.argv != NULL) {
				warnx("multiple ignores specified");
				imsg_free(&msg);
				goto msgbuf;
			}
			if (recv_argv(&msg, &ignore.argc, &ignore.argv) == -1) {
				warnx("failed to receive ignore headers");
				imsg_free(&msg);
				goto msgbuf;
			}
			switch (imsg_get_type(&msg)) {
			case IMSG_MDR_IGNORE:
				ignore.type = IGNORE_IGNORE;
				break;
			case IMSG_MDR_RETAIN:
				ignore.type = IGNORE_RETAIN;
				break;
			}
			break;
		case IMSG_MDR_REORDER:
			if (recv_argv(&msg, &reorder.argc, &reorder.argv) == -1) {
				warnx("failed to receive reorder headers");
				imsg_free(&msg);
				goto msgbuf;
			}
			break;
		}

		imsg_free(&msg);
	}

	memset(&gl, 0, sizeof(gl));
	memset(&headers, 0, sizeof(headers));

	for (;;) {
		struct header header;
		int hv;

		if ((hv = header_read(stdin, &gl, &header, 1)) == HEADER_EOF)
			break;
		if (hv == HEADER_ERR) {
			warnx("failed to read header");
			goto headers;
		}

		if (strcasecmp(header.key, "content-type") != 0
				&& strcasecmp(header.key, "content-transfer-encoding") != 0
				&& header_ignore(header.key, &ignore)) {
			free(header.key);
			free(header.val);
			continue;
		}

		find.key = header.key;
		if ((found = RB_FIND(headers, &headers, &find)) != NULL) {
			size_t clen, olen, nlen;
			char *t;

			clen = strlen(header.val);
			olen = strlen(found->val);
			nlen = clen + olen;

			if ((t = realloc(found->val, nlen + 1)) == NULL) {
				warn("realloc");
				free(header.key);
				free(header.val);
				goto headers;
			}

			found->val = t;
			memcpy(&found->val[olen], header.val, clen + 1); /* includes NUL */

			free(header.key);
			free(header.val);
		}
		else {
			struct header_rb *rb;

			if ((rb = malloc(sizeof(*rb))) == NULL) {
				warn("malloc");
				free(header.key);
				free(header.val);
				goto headers;
			}

			rb->key = header.key;
			rb->val = header.val;

			/* 
			 * This will always return NULL since the above RB_FIND did,
			 * so rb will not be leaked.
			 */
			(void)RB_INSERT(headers, &headers, rb); //NOLINT(clang-analyzer-unix.Malloc)
		}
	}

	encoding = ENCODING_PLAIN;

	find.key = "content-transfer-encoding";
	if ((found = RB_FIND(headers, &headers, &find)) != NULL) {
		if (!strcmp(found->val, "quoted-printable"))
			encoding = ENCODING_QP;
		if (header_ignore("content-transfer-encoding", &ignore)) {
			RB_REMOVE(headers, &headers, found);
			free(found->key);
			free(found->val);
			free(found);
		}
	}

	charset = CHARSET_ASCII;

	find.key = "content-type";
	if ((found = RB_FIND(headers, &headers, &find)) != NULL) {
		struct content_type ct;
		char *cts;

		if (header_ignore("content-type", &ignore)) {
			cts = found->val;

			RB_REMOVE(headers, &headers, found);
			free(found->key);
			free(found);
		}
		else {
			if ((cts = strdup(found->val)) == NULL) {
				warn("strdup");
				goto headers;
			}
		}

		if (content_type_parse(cts, &ct) == -1) {
			warnx("failed to parse content-type");
			free(cts);
			goto headers;
		}

		if (ct.charset != NULL) {
			if (!strcasecmp(ct.charset, "us-ascii"))
				charset = CHARSET_ASCII;
			else if (!strcasecmp(ct.charset, "utf-8"))
				charset = CHARSET_UTF8;
			else
				charset = CHARSET_OTHER;
		}

		free(cts);
	}

	for (size_t i = 0; i < reorder.argc; i++) {
		find.key = reorder.argv[i];

		if ((found = RB_FIND(headers, &headers, &find)) != NULL) {
			if (printf("%s: %s\n", found->key, found->val) < 0) {
				warn("printf");
				goto headers;
			}
			RB_REMOVE(headers, &headers, found);
			free(found->key);
			free(found->val);
			free(found);
		}
	}

	RB_FOREACH(h, headers, &headers) {
		if (printf("%s: %s\n", h->key, h->val) < 0) {
			warn("printf");
			goto headers;
		}
	}

	if (putchar('\n') == EOF) {
		warn("putchar");
		goto headers;
	}

	memset(&mbs, 0, sizeof(mbs));
	for (;;) {
		char buf[4];
		int n;

		if ((n = get_char(charset, encoding, &mbs, buf)) == 0)
			break;
		if (n == -1) {
			warnx("failed to get character from letter");
			goto headers;
		}

		if (fwrite(buf, n, 1, stdout) != 1) {
			warn("fwrite");
			goto headers;
		}
	}

	if (fflush(stdout) == EOF) {
		warn("fflush");
		goto headers;
	}

	rv = 0;
	headers:
	free(gl.line);
	RB_FOREACH_SAFE(h, headers, &headers, h2) {
		RB_REMOVE(headers, &headers, h);
		free(h->key);
		free(h->val);
		free(h);
	}
	msgbuf:
	for (size_t i = 0; i < ignore.argc; i++)
		free(ignore.argv[i]);
	free(ignore.argv);
	for (size_t i = 0; i < reorder.argc; i++)
		free(reorder.argv[i]);
	free(reorder.argv);
	imsg_clear(&msgbuf);
	fd:
	if (close(3) == -1) {
		warn("close");
		rv = 1;
	}
	if (getdtablecount() != 3) {
		warnx("fd leak %d", getdtablecount() - 3);
		rv = 1;
	}
	return rv;
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

		param += strspn(param, " \t");

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
getbyte(enum encoding encoding)
{
	int c, hi, lo;

	again:
	if ((c = getchar()) == EOF)
		return GB_EOF;
	switch (encoding) {
	case ENCODING_PLAIN:
		return c;
	case ENCODING_QP:
		if (c != '=')
			return c;
		if ((hi = getchar()) == EOF) {
			warnx("EOF inside quoted-printable body");
			return GB_ERR;
		}

		if (hi == '\n') /* soft break */
			goto again;

		if ((lo = getchar()) == EOF) {
			warnx("EOF inside quoted-printable body");
			return GB_ERR;
		}

		if ((hi = hexdigcaps(hi)) == -1 || (lo = hexdigcaps(lo)) == -1) {
			warnx("non hexadecimal character in quoted-printable body");
			return GB_ERR;
		}

		return (hi << 4) | lo;
	default:
		/* NOTREACHED */
		abort();
	}
}

static int
get_char(enum charset charset, enum encoding encoding, mbstate_t *mbs,
	char buf[static 4])
{
	int i;

	for (i = 0; i < 4; i++) {
		int c;
		char cc;

		if ((c = getbyte(encoding)) == GB_EOF) {
			if (charset == CHARSET_UTF8 && i != 0) {
				warnx("EOF while decoding");
				return -1;
			}

			return 0;
		}

		switch (charset) {
		case CHARSET_ASCII:
			/* 
			 * Because multipart messages are not yet parsed correctly
			 * "ascii" must be treated as potentially any encoding
			 */
			#ifdef notyet
			if (!isascii(c)) {
				warnx("non ascii character in message body");
				return -1;
			}
			if (!isprint(c) && !isspace(c))
				goto replace;
			buf[0] = c;
			return 1;
			#endif
		case CHARSET_OTHER:
			if (!isascii(c) || (!isprint(c) && !isspace(c)))
				goto replace;
			buf[0] = c;
			return 1;
		case CHARSET_UTF8:
			break;
		}

		cc = c;
		switch (mbrtowc(NULL, &cc, 1, mbs)) {
		case -1:
		case -3: /* maybe should be with -2 */
			warnx("invalid UTF-8 in message body");
			return -1;
		case -2:
			buf[i] = c;
			continue;
		default:
			buf[i] = c;

			if (i == 0) {
				if (!isascii(c)) {
					/* probably not possible */
					warnx("non ascii character in message body (%x)", c);
					return -1;
				}
				if (!isprint(c) && !isspace(c))
					goto replace;
			}
			return i + 1;
		}
	}

	/* probably cant happen */
	warnx("overlong UTF8 encoding");
	return -1;

	replace:
	memcpy(buf, "\xEF\xBF\xBD", 3);
	return 3;
}

static int
header_ignore(const char *key, struct ignore *ignore)
{
	int rv;

	switch (ignore->type) {
	case IGNORE_ALL:
		return 1;
	case IGNORE_IGNORE:
		rv = 1;
		break;
	case IGNORE_RETAIN:
		rv = 0;
		break;
	default:
		/* NOTREACHED */
		abort();
	}

	for (size_t i = 0; i < ignore->argc; i++)
		if (!strcasecmp(key, ignore->argv[i]))
			return rv;
	return !rv;
}

static int
header_rb_cmp(struct header_rb *one, struct header_rb *two)
{
	return strcasecmp(one->key, two->key);
}

RB_GENERATE_STATIC(headers, header_rb, entries, header_rb_cmp);

static int
hexdigcaps(int c)
{
	if (isdigit(c))
		return c - '0';
	if (isupper(c))
		return c - 'A' + 10;
	else
		return -1;
}

static int
recv_argv(struct imsg *msg, size_t *out_argc, char ***out_argv)
{
	struct ibuf ibuf;
	size_t argc;
	char **argv;
	int rv;

	rv = -1;
	if (imsg_get_ibuf(msg, &ibuf) == -1) {
		warn("parent sent bogus imsg (no data)");
		return -1;
	}

	argc = *out_argc;
	argv = *out_argv;
	while (ibuf_size(&ibuf) != 0) {
		char *s, **t;

		if (ibuf_get_string(&ibuf, &s) == -1) {
			warnx("ibuf_get_string");
			goto fail;
		}

		t = reallocarray(argv, argc + 1, sizeof(*argv));
		if (t == NULL) {
			warn("reallocarray");
			free(s);
			goto fail;
		}
		argv = t;
		argv[argc++] = s;
	}

	rv = 0;
	fail:
	*out_argc = argc;
	*out_argv = argv;
	return rv;
}
