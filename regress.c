/*
 * Copyright (c) 2024 Henry Ford <fordhenry2299@gmail.com>

 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.

 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/wait.h>

#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "date.h"
#include "cache.h"
#include "charset.h"
#include "conf.h"
#include "content-type.h"
#include "encoding.h"
#include "extract.h"
#include "from.h"
#include "header.h"
#include "letter.h"
#include "maildir.h"
#include "pathnames.h"
#include "printable.h"
#include "read-letter.h"
#include "read-letters.h"
#include "string-util.h"
#include "util.h"

struct test {
	const char *ident;
	int (*fn) (void);
};

static int date_format_test(void);
static int date_parse_test(void);
static int cache_test(void);
static int charset_test(void);
static int config_test(void);
static int content_type_test(void);
static int encoding_test(void);
static int extract_test(void);
static int from_test(void);
static int header_test(void);
static int letter_cmp(struct letter *, struct letter *);
static int letters_test(void);
static int maildir_seen_test(void);
static int maildir_setflag_test(void);
static int printable_test(void);
static int read_letter_test(void);
static const char *plural(size_t);
static int run_test(const char *, const char *);
static int strcmp_null(const char *, const char *);
static const char *strseen(int);
static int test_cmp(const void *, const void *);
static void usage(void);

#define min(a, b) ((a) < (b) ? (a) : (b))

static struct test tests[] = {
	{ "cache", cache_test },
	{ "charset", charset_test },
	{ "config", config_test },
	{ "content_type", content_type_test },
	{ "date_format", date_format_test },
	{ "date_parse", date_parse_test },
	{ "encoding", encoding_test },
	{ "extract", extract_test },
	{ "from", from_test },
	{ "header", header_test },
	{ "letters", letters_test },
	{ "maildir_seen", maildir_seen_test },
	{ "maildir_setflag", maildir_setflag_test },
	{ "printable", printable_test },
	{ "read_letter", read_letter_test },
};

int
main(int argc, char *argv[])
{
	const char *progname;
	size_t nbad, ngood;
	int ch;

	progname = argv[0];
	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 1) {
		const struct test *test;

		test = bsearch(argv[0], tests, nitems(tests), sizeof(*tests), 
			test_cmp);
		if (test == NULL)
			errx(1, "unknown test '%s'", argv[0]);

		if (test->fn() == -1) {
			warnx("test '%s' failed", test->ident);
			return 1;
		}

		return 0;
	}

	if (pledge("stdio proc exec", NULL) == -1)
		err(1, "pledge");

	nbad = ngood = 0;
	if (argc > 0) {
		for (int i = 0; i < argc; i++) {
			if (run_test(progname, argv[i]) == -1)
				nbad++;
			else
				ngood++;
		}
	}
	else {
		for (size_t i = 0; i < nitems(tests); i++) {
			if (run_test(progname, tests[i].ident) == -1)
				nbad++;
			else
				ngood++;
		}
	}

	printf("%zu test%s succeeded, %zu test%s failed\n", 
		ngood, plural(ngood), nbad, plural(nbad));
}

static int
date_format_test(void)
{
	struct {
		time_t date;
		long offh;
		long offm;
		const char *out;
	} dates[] = {
		{ 1724585251, 2, 0, "Sun, 25 Aug 2024 13:27:31 +0200" },
		{ 1724585251, 0, 0, "Sun, 25 Aug 2024 11:27:31 +0000" },
		{ 1724585251, -2, 0, "Sun, 25 Aug 2024 09:27:31 -0200" },
	};

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	for (size_t i = 0; i < nitems(dates); i++) {
		struct tm *tm;
		long off;
		char buf[EMAIL_DATE_LEN];

		if ((tm = gmtime(&dates[i].date)) == NULL)
			err(1, "localtime");

		tm->tm_hour += dates[i].offh;
		tm->tm_min += dates[i].offm;

		off = (dates[i].offh * 60 * 60) + (dates[i].offm * 60);

		if (date_format(tm, off, buf) == -1)
			errx(1, "date formatting failed");

		if (strcmp(buf, dates[i].out) != 0)
			errx(1, "date was %s, should have been %s", buf, 
				dates[i].out);
	}
	return 0;
}

static int
date_parse_test(void)
{
	char date1[] = "Sun, 25 Aug 2024 13:27:31 +0200";
	char date2[] = "Sun, 25 Aug 2024 13:27:31 +0200 (GST)";
	char date3[] = "25 Aug 2024 13:27:31 +0200";
	struct {
		char *raw;
		time_t date;
	} dates[] = {
		{ date1, 1724585251 },
		{ date2, 1724585251 },
		{ date3, 1724585251 },
	};

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	for (size_t i = 0; i < nitems(dates); i++) {
		time_t date;

		if ((date = date_parse(dates[i].raw)) == -1) {
			warnx("failed to parse date");
			return -1;
		}
		if (date != dates[i].date) {
			warnx("date should have been %lld, was %lld", 
				(long long)dates[i].date, (long long)date);
			return -1;
		}
	}

	if (date_parse("hello") != -1)
		return -1;

	return 0;
}

static int
cache_test(void)
{
	struct cache cache;
	struct letter letter;
	FILE *fp;
	int rv;

	rv = -1;

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		err(1, "setlocale");
	if ((fp = tmpfile()) == NULL)
		return -1;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	letter.date = 16;
	letter.from.addr = "dave@gnu.org";
	letter.from.name = "Dave";
	letter.path = "abcdef";
	letter.subject = "Hello";

	if (cache_write(1, -1, fp, &letter, 1) == -1)
		goto fp;
	if (fseek(fp, 0, SEEK_SET) == -1)
		goto fp;

	if (cache_read(fp, &cache) == -1)
		goto fp;
	if (cache.nletter != 1)
		goto cache;

	if (letter_cmp(&cache.letters[0], &letter) != 0)
		goto cache;

	rv = 0;
	cache:
	cache_free(&cache);
	fp:
	fclose(fp);
	return rv;
}

static int
charset_test(void)
{
	struct {
		char *raw;
		const char *out;
		const char *charset;
		const char *encoding;
	} charsets[] = {
		{ "hello", "hello", "us-ascii", "7bit" },
		{ "hel=6Co", "hello", "us-ascii", "quoted-printable" },
		{ "\xf0\x9f\x90\xb7", "\xf0\x9f\x90\xb7", "UtF-8", "8bit" },
		{ "=F0=9F=90=B7", "\xf0\x9f\x90\xb7", "utf-8", "quoted-printable" },
	};

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		err(1, "setlocale");

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	for (size_t i = 0; i < nitems(charsets); i++) {
		FILE *fp;
		const char *out;
		struct charset charset;

		if (charset_set(&charset, charsets[i].charset, 
			strlen(charsets[i].charset)) == -1)
				return -1;
		if (encoding_set(&charset.encoding, 
			charsets[i].encoding) == -1)
				return -1;

		if ((fp = fmemopen(charsets[i].raw, strlen(charsets[i].raw),
			"r")) == NULL)
				return -1;

		out = charsets[i].out;

		for (;;) {
			char buf[4];
			int n;

			if ((n = charset_getchar(fp, &charset, buf)) == -1) {
				warnx("charset invalid");
				goto fp;
			}

			if (n == 0) {
				if (*out != '\0') {
					warnx("charset early EOF");
					goto fp;
				}
				break;
			}

			if (strlen(out) < (size_t)n) {
				warnx("charset extra data");
				goto fp;
			}
			if (memcmp(out, buf, (size_t)n) != 0) {
				warnx("charset invalid output %d %.*s %s", n, n, buf, out);
				goto fp;
			}
			out += n;
		}

		fclose(fp);
		continue;

		fp:
		fclose(fp);
		return -1;
	}

	return 0;
}

#define PATH_TEST_CONF "regress/tests/mailz.conf"

static int
config_test(void)
{
	struct mailz_conf conf;
	FILE *fp;
	char *headers[] = {	"Subject", "From", "To", "Cc", 
						"Organization", "Date", "List-ID" };
	int rv;

	rv = -1;
	if ((fp = fopen(PATH_TEST_CONF, "r")) == NULL)
		err(1, "%s", PATH_TEST_CONF);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (config_init(&conf, fp, PATH_TEST_CONF) == -1) {
		fclose(fp);
		errx(1, "configuration failed");
	}

	if (!conf.cache.enabled)
		goto conf;

	if (conf.address.addr == NULL 
		|| strcmp(conf.address.addr, "fordhenry2299@gmail.com") != 0)
			goto conf;

	if (conf.address.name == NULL
		|| strcmp(conf.address.name, "Henry Ford") != 0)
			goto conf;

	if (conf.ignore.type != IGNORE_RETAIN)
		goto conf;
	if (conf.ignore.argc != nitems(headers))
		goto conf;

	for (size_t i = 0; i < nitems(headers); i++)
		if (strcmp(headers[i], conf.ignore.argv[i]) != 0)
			goto conf;

	if (conf.reorder.argc != nitems(headers))
		goto conf;

	for (size_t i = 0; i < nitems(headers); i++)
		if (strcmp(headers[i], conf.reorder.argv[i]) != 0)
			goto conf;

	rv = 0;
	conf:
	config_free(&conf);
	fclose(fp);
	return rv;
}

static int
content_type_test(void)
{
	struct vars {
		const char *key;
		const char *val;
	} vars1[] = {
		{ "charset", "us-ascii" },
		{ NULL, NULL },
	}, vars2[] = {
		{ "charset", "us-ascii" },
		{ "x-caring", "no" },
		{ NULL, NULL },
	};
	struct {
		const char *raw;
		const char *type;
		const char *subtype;
		struct vars *vars;
	} types[] = {
		{ "text/image", "text", "image", NULL },
		{ "text/image; charset=us-ascii", "text", "image", vars1 },
		{ "text/image; charset=\"us-ascii\"", "text", "image", vars1 },
		{ "text/image; charset=us-ascii; x-caring=no", "text", "image", vars2 },
		{ "text/ferrets charset=hello!", NULL, NULL, NULL },
	};

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	for (size_t i = 0; i < nitems(types); i++) {
		struct content_type type;
		struct content_type_var var;

		if (content_type_init(&type, types[i].raw) == -1) {
			if (types[i].type == NULL)
				continue;
			warnx("content type parsed as invalid");
			return -1;
		}

		if (types[i].type == NULL) {
			warnx("content type parsed as valid but shouldnt have");
			return -1;
		}

		if (bounded_strcmp(types[i].type, type.type, type.type_len) != 0) {
			warnx("type was %.*s, should have been %s",
				(int)min(type.subtype_len, INT_MAX), type.type, types[i].type);
			return -1;
		}

		if (bounded_strcmp(types[i].subtype, type.subtype, type.subtype_len) != 0) {
			warnx("subtype was %.*s, should have been %s", 
				(int)min(type.subtype_len, INT_MAX), type.subtype, types[i].subtype);
		}

		for (struct vars *v = types[i].vars; v != NULL && v->key != NULL; v++) {
			int n;

			if ((n = content_type_next(&type, &var)) == -1) {
				warnx("variable was parsed as invalid");
				return -1;
			}

			if (n == 0) {
				warnx("variable was not present");
				return -1;
			}

			if (bounded_strcmp(v->key, var.key, var.key_len) != 0) {
				warnx("variable key was %.*s, should have been %s",
					(int)min(INT_MAX, var.key_len), var.key, v->key);
				return -1;
			}

			if (bounded_strcmp(v->val, var.val, var.val_len) != 0) {
				warnx("variable value was %.*s, should have been %s",
					(int)min(INT_MAX, var.val_len), var.val, v->key);
				return -1;
			}
		}

		if (content_type_next(&type, &var) != 0) {
			warnx("extra variables");
			return -1;
		}
	}
	return 0;
}

static int
encoding_test(void)
{
	char invalidc;
	const char *invalid = &invalidc;
	const struct {
		char *raw;
		const char *out;
		const char *type;
	} encodings[] = {
		{ "hello", "hello", "7bit" },
		{ "hel=\n=6Co", "hello", "quoted-printable" },
		{ "aGVsbG8=", "hello", "base64" },

		{ "\xff", invalid, "7bit" },
	};

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	for (size_t i = 0; i < nitems(encodings); i++) {
		FILE *fp;
		const char *out;
		struct encoding encoding;

		if (encoding_set(&encoding, encodings[i].type) == -1)
			return -1;
		out = encodings[i].out;

		if ((fp = fmemopen(encodings[i].raw, strlen(encodings[i].raw), 
			"r")) == NULL) {
				warn("fmemopen");
				return -1;
			}

		for (;;) {
			int c;

			if ((c = encoding_getbyte(fp, &encoding)) == ENCODING_ERR) {
				if (encodings[i].out == invalid)
					break;
				warnx("encoding returned error");
				goto fp;
			}

			if (encodings[i].out == invalid) {
				warnx("encoding should have been invalid but wasnt");
				goto fp;
			}

			if (c == ENCODING_EOF) {
				if (*out != '\0') {
					warnx("early EOF");
					goto fp;
				}
				break;
			}

			if (*out == '\0') {
				warnx("encoding returned too much data");
				goto fp;
			}

			if (*out++ != c) {
				warnx("encoding invalid");
				goto fp;
			}
		}

		fclose(fp);
		continue;

		fp:
		fclose(fp);
		return -1;
	}

	return 0;
}

static int
extract_test(void)
{
	struct extracted_header headers[5];
	int fd, rv;

	rv = -1;
	if ((fd = open("regress/tests/letter", O_RDONLY | O_CLOEXEC)) == -1) {
		warn("tests/letter");
		return -1;
	}

	if (unveil(PATH_MAILDIR_EXTRACT, "x") == -1)
		err(1, "%s", PATH_MAILDIR_EXTRACT);
	if (pledge("stdio proc exec sendfd", NULL) == -1)
		err(1, "pledge");

	signal(SIGPIPE, SIG_IGN);

	headers[0].key = "From";
	headers[0].type = EXTRACT_FROM;

	headers[1].key = "Date";
	headers[1].type = EXTRACT_DATE;

	headers[2].key = "Subject";
	headers[2].type = EXTRACT_STRING;

	headers[3].key = "Message-ID";
	headers[3].type = EXTRACT_MESSAGE_ID;

	headers[4].key = "X-missing";
	headers[4].type = EXTRACT_STRING;

	if (maildir_extract_quick(fd, headers, nitems(headers)) == -1)
		return -1;

	if (headers[0].val.from.addr == NULL 
		|| strcmp(headers[0].val.from.addr, "dave@gnu.org") != 0)
			goto fail;

	if (headers[0].val.from.name == NULL 
		|| strcmp(headers[0].val.from.name, "Dave") != 0)
			goto fail;

	if (headers[1].val.date != 1724585251)
		goto fail;

	if (headers[2].val.string == NULL
		|| strcmp(headers[2].val.string, "Hello") != 0)
			goto fail;

	if (headers[3].val.string == NULL
		|| strcmp(headers[3].val.string, "random@gnu.org") != 0)
			goto fail;

	if (headers[4].val.string != NULL)
		goto fail;

	rv = 0;
	fail:
	for (size_t i =  0; i < nitems(headers); i++)
		extract_header_free(headers[i].type, &headers[i].val);
	return rv;
}

static int
from_test(void)
{
	struct from from;
	const struct {
		char *raw;
		const char *addr;
		const char *name;
	} addrs[] = {
		{ "gary@gnu.org", "gary@gnu.org", "" },
		{ "<gary@gnu.org>", "gary@gnu.org", "" },
		{ "Gary <gary@gnu.org>", "gary@gnu.org", "Gary" },
	};

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	for (size_t i = 0; i < nitems(addrs); i++) {
		if (from_parse(addrs[i].raw, &from) == -1)
			return -1;

		if (bounded_strcmp(addrs[i].addr, from.addr, from.al) != 0) {
			warnx("addr was %.*s, should have been %s",
				(int)min(INT_MAX, from.al), from.addr, addrs[i].addr);
			return -1;
		}

		if (bounded_strcmp(addrs[i].name, from.name, from.nl) != 0) {
			warnx("addr was %.*s, should have been %s",
				(int)min(INT_MAX, from.nl), from.name, addrs[i].addr);
			return -1;
		}
	}

	if (from_parse("<invalid", &from) != -1)
		return -1;

	return 0;
}

static int
header_test(void)
{
	struct getline gl;
	const struct {
		char *raw;
		const char *key;
		const char *val;
	} headers[] = {
		{ "From: A true friend\n", "From", "A true friend" },
		{ "From:A true friend\n", "From", "A true friend" },
		{ "From: A true\n friend\n", "From", "A true friend" },
	};
	int rv;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	memset(&gl, 0, sizeof(gl));

	rv = 0;
	for (size_t i = 0; i < nitems(headers); i++) {
		struct header header;
		FILE *fp;

		if ((fp = fmemopen(headers[i].raw, strlen(headers[i].raw), 
			"r")) == NULL) {
				warn("fmemopen");
				goto fail;
		}
		switch (header_read(fp, &gl, &header, 1)) {
		case HEADER_EOF:
			warnx("header_read returned EOF");
			goto fp;
		case HEADER_ERR:
			warn("header_read returned error");
			goto fp;
		default:
			break;
		}

		if (strcmp(headers[i].key, header.key) != 0) {
			warnx("key should be %s, is %s", headers[i].key, header.key);
			goto header;
		}

		if (strcmp(headers[i].val, header.val) != 0) {
			warnx("val should be %s, is %s", headers[i].val, header.val);
			goto header;
		}

		fclose(fp);
		free(header.key);
		free(header.val);

		continue;

		header:
		free(header.key);
		free(header.val);
		fp:
		fclose(fp);
		rv = -1;
		goto fail;
	}

	fail:
	free(gl.line);
	return rv;
}

static int
letter_cmp(struct letter *one, struct letter *two)
{
	if (one->date != two->date)
		return 1;
	if (strcmp(one->from.addr, two->from.addr) != 0)
		return 1;

	if (strcmp_null(one->from.name, two->from.name) != 0)
		return 1;

	if (strcmp(one->path, two->path) != 0)
		return 1;
	if (strcmp_null(one->subject, two->subject) != 0)
		return 1;

	return 0;
}

#define PATH_TEST_LETTERS "./regress/tests/inbox/"

static int
letters_test(void)
{
	struct letter *letters;
	size_t nletter;
	int cur, root, rv;

	rv = -1;

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		err(1, "setlocale");

	if ((root = open(PATH_TEST_LETTERS, 
		O_RDONLY | O_DIRECTORY | O_CLOEXEC)) == -1)
			return -1;
	if ((cur = openat(root, "cur", 
		O_RDONLY | O_DIRECTORY | O_CLOEXEC)) == -1)
			goto root;

	if (unveil(PATH_TEST_LETTERS, "r") == -1)
		err(1, "%s", PATH_TEST_LETTERS);
	if (unveil(PATH_MAILDIR_EXTRACT, "x") == -1)
		err(1, "%s", PATH_MAILDIR_EXTRACT);
	if (pledge("stdio proc sendfd exec rpath", NULL) == -1)
		err(1, "pledge");

	if (letters_read(root, cur, 1, &letters, &nletter) == -1)
		goto cur;

	/* XXX: more validation */

	rv = 0;
	for (size_t i = 0; i < nletter; i++)
		letter_free(&letters[i]);
	free(letters);
	cur:
	close(cur);
	root:
	close(root);
	return rv;
}

static int
maildir_seen_test(void)
{
	struct {
		const char *path;
		int seen;
	} seens[] = {
		{ "blahblah:2,S", 1 },
		{ "blahblah:2,", 0 },
		{ "blahblah", 0 },
		{ "blahblah:1,", 0 },
	};

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	for (size_t i = 0; i < nitems(seens); i++) {
		int seen;

		seen = maildir_letter_seen(seens[i].path);
		if (seen != seens[i].seen) {
			warnx("%s was %s, should have been %s", seens[i].path,
				strseen(seen), strseen(seens[i].seen));
			return -1;
		}
	}

	return 0;
}

static int
maildir_setflag_test(void)
{
	char invalidc;
	const char *invalid = &invalidc;
	struct {
		char *old;
		const char *new;
		int flag;
		int set;
	} setflags[] = {
		{ "set:2,", "set:2,S", 'S', 1 },
		{ "set:2,JT", "set:2,JST", 'S', 1 },
		{ "set:2,S", NULL, 'S', 1 },

		{ "set:1,", invalid, 'S', 1 },
		{ "set", invalid, 'S', 1 },

		{ "unset:2,SJ", "unset:2,J", 'S', 0 },
		{ "unset:2,", NULL, 'S', 0 },
	};

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	for (size_t i = 0; i < nitems(setflags); i++) {
		char *new;

		if (setflags[i].set)
			new = maildir_setflag(setflags[i].old, setflags[i].flag);
		else
			new = maildir_unsetflag(setflags[i].old, setflags[i].flag);

		if (new == NULL) {
			if (setflags[i].new == invalid)
				continue;
			warnx("maildir_setflag failed");
			return -1;
		}

		if (setflags[i].new == invalid) {
			warnx("was %s, should have been invalid", new);
			if (new != setflags[i].old)
				free(new);
			return -1;
		}

		if (setflags[i].new == NULL && new != setflags[i].old) {
			warn("was %s, should have been %s", new, setflags[i].old);
			free(new);
			return -1;
		}

		if (setflags[i].new != NULL && strcmp(setflags[i].new, new) != 0) {
			warnx("was %s, should have been %s", new,
				setflags[i].new == NULL ? setflags[i].old : setflags[i].new);
			if (new != setflags[i].old)
				free(new);
			return -1;
		}

		if (new != setflags[i].old)
			free(new);
	}

	return 0;
}

static int
printable_test(void)
{
	struct {
		const char *s;
		int isprint;
	} strings[] = {
		{ "hello", 1 },
		{ "\xf0\x9f\x90\xb7pig", 1 },
		{ "\x07" "bel", 0 },
		{ "\x1b" "esc", 0 },
	};

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		err(1, "setlocale");
	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	for (size_t i = 0; i < nitems(strings); i++) {
		int isprint;

		isprint = string_isprint(strings[i].s);

		if (isprint != strings[i].isprint) {
			warnx("string was %sprintable, should have been %sprintable",
				!isprint ? "not " : "", !strings[i].isprint ? "not " : "");
			return -1;
		}
	}

	return 0;
}

static int
read_letter_test(void)
{
	struct read_letter rl;
	struct ignore ignore;
	struct reorder reorder;
	char buf[4];
	int n, fd, rv;

	rv = -1;

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		err(1, "setlocale");

	if ((fd = open("regress/tests/letter", O_RDONLY | O_CLOEXEC)) == -1)
		return -1;
	if (unveil(PATH_MAILDIR_READ_LETTER, "x") == -1)
		err(1, "%s", PATH_MAILDIR_READ_LETTER);

	if (pledge("stdio proc exec", NULL) == -1)
		err(1, "pledge");

	memset(&ignore, 0, sizeof(ignore));
	memset(&reorder, 0, sizeof(reorder));
	if (maildir_read_letter(&rl, fd, 0, 0, &ignore, &reorder) == -1)
		goto rl;

	if (pledge("stdio proc", NULL) == -1)
		err(1, "pledge");

	while ((n = maildir_read_letter_getc(&rl, buf)) != 0)
		if (n == -1)
			goto rl;

	rv = 0;
	rl:
	if (maildir_read_letter_close(&rl) == -1)
		rv = -1;
	close(fd);
	return rv;
}

static const char *
plural(size_t n)
{
	if (n == 1)
		return "";
	return "s";
}

static int
run_test(const char *progname, const char *ident)
{
	pid_t pid;
	int status;

	switch (pid = fork()) {
	case -1:
		warn("fork");
		return -1;
	case 0:
		execl(progname, "regress", ident, NULL);
		err(1, "%s", progname);
	default:
		break;
	}

	if (waitpid(pid, &status, 0) == -1 
		|| WEXITSTATUS(status) != 0 || WIFSIGNALED(status))
		return -1;

	return 0;
}

static int
strcmp_null(const char *one, const char *two)
{
	if ((one == NULL) != (two == NULL))
		return 1;
	if (one != NULL)
		return strcmp(one, two);

	/* both NULL */
	return 0;
}

static const char *
strseen(int seen)
{
	switch (seen) {
	case 0:
		return "not seen";
	default:
		return "seen";
	}
}

static int
test_cmp(const void *one, const void *two)
{
	const char *n1 = one;
	const struct test *n2 = two;

	return strcmp(n1, n2->ident);
}

static void
usage(void)
{
	fprintf(stderr, "usage: regress [tests ...]\n");
	exit(2);
}
