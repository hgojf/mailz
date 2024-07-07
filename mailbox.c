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

#define MAILBOX_INTERNALS

#include <sys/stat.h>
#include <sys/tree.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "address.h"
#include "mail.h"
#include "mailbox.h"

struct content_type {
	char *type;
	char *subtype;

	char *charset;
};

struct getline {
	char *line;
	size_t n;
};

struct header {
	char *key;
	char *val;
	RB_ENTRY(header) entry;
};

static int header_cmp(struct header *, struct header *);
RB_HEAD(headers, header);
RB_PROTOTYPE(headers, header, entry, header_cmp);
RB_GENERATE(headers, header, entry, header_cmp);

static int equal_escape(FILE *, int);

static FILE *fopenat(int, const char *);
static DIR *opendirat(int, const char *);

static int header_ignore(struct header *, const struct options *);
static int header_push(struct header *, struct letter *);
static int header_push2(struct header *, struct headers *);
static int header_read(FILE *, struct getline *, struct header *);
static int header_read1(FILE *, struct getline *, struct header *, int);

static int letter_cmp(const void *, const void *);
static void letter_free(struct letter *);
static int letter_push(struct letter *, struct mailbox *);
static int letter_read(FILE *, struct letter *, struct getline *);

struct maildir_cache_letter {
	char *path;
	char *subject;
	struct from_safe from;
	time_t date;
};

struct maildir_cache {
	time_t mtim;
	int view_seen;

	size_t nletters;
	struct maildir_cache_letter *letters;
};

#define MAILDIR_CACHE_CHECK_MASK 	0xFFFFFF00
#define MAILDIR_CACHE_VERSION_MASK 	0x000000FF
#define MAILDIR_CACHE_MAGIC			0x48414D00
#define MAILDIR_CACHE_VERSION		0

static struct maildir_cache_letter 
	*maildir_cache_find(struct maildir_cache *, const char *);
static int maildir_cache_read(const char *, struct maildir_cache *);
static int maildir_cache_write(struct mailbox *);

static DIR *maildir_setup(int);
static int maildir_letter_set_flag(DIR *, struct letter *, char);
static int maildir_letter_unset_flag(DIR *, struct letter *, char);
static int maildir_letter_seen(const char *);

#define TZ_INVALIDSEC 1

static int content_type_parse(char *, struct content_type *);
static char *dupstr(const char *, size_t);
static time_t rfc5322_dateparse(const char *);
static char *strip_trailing(char *);

int
mailbox_setup(const char *path, struct mailbox *out)
{
	int fd;

	if ((fd = open(path, O_RDONLY | O_CLOEXEC | O_DIRECTORY)) == -1) {
		warn("open %s", path);
		return -1;
	}

	if ((out->cur = maildir_setup(fd)) == NULL) {
		(void) close(fd);
		return -1;
	}

	if (close(fd) == -1) {
		(void) closedir(out->cur);
		return -1;
	}

	out->root = path;
	out->view_seen = 0;
	out->do_cache = 0;

	return 0;
}

int
mailbox_read(struct mailbox *out, int view_seen, int do_cache)
{
	struct stat sb;
	struct maildir_cache cache;
	struct getline gl;
	DIR *mdir;
	int have_cache, dfd, rv;

	rv = -1;

	mdir = out->cur;
	dfd = dirfd(mdir);

	out->letters = NULL;
	out->nletters = 0;

	out->view_seen = view_seen;

	gl.line = NULL;
	gl.n = 0;

	if (!do_cache || maildir_cache_read(out->root, &cache) == -1) {
		have_cache = 0;
	}
	else
		have_cache = 1;

	if (fstat(dfd, &sb) == -1)
		return -1;

	/* no change since last cache, just take all letters */
	if (have_cache
			&& (!view_seen || cache.view_seen)
			&& fstat(dfd, &sb) != -1
			&& sb.st_mtim.tv_sec <= cache.mtim) {
		void *t;

		out->letters = reallocarray(NULL, cache.nletters, sizeof(*out->letters));
		if (out->letters == NULL) {
			for (size_t i = 0; i < cache.nletters; i++) {
				free(cache.letters[i].path);
				free(cache.letters[i].subject);
				free(cache.letters[i].from.str);
			}
			free(cache.letters);
			return -1;
		}

		for (size_t i = 0; i < cache.nletters; i++) {
			if (!view_seen && maildir_letter_seen(cache.letters[i].path)) {
				free(cache.letters[i].path);
				free(cache.letters[i].subject);
				free(cache.letters[i].from.str);
				continue;
			}

			out->letters[out->nletters].path = cache.letters[i].path;
			out->letters[out->nletters].subject = cache.letters[i].subject;
			out->letters[out->nletters].from = cache.letters[i].from;
			out->letters[out->nletters].date = cache.letters[i].date;
			out->nletters++;
		}
		free(cache.letters);

		/* 
		 * try to shrink in case some already seen letters were present
		 * if we cant shrink thats fine
		 */
		t = reallocarray(out->letters, out->nletters, sizeof(*out->letters));
		if (t != NULL)
			out->letters = t;

		qsort(out->letters, out->nletters, sizeof(*out->letters),
			letter_cmp);

		out->do_cache = MAILBOX_CACHE_READ;

		return 0;
	}
	else if (do_cache)
		/* something has changed, so the cache needs to be updated */
		out->do_cache = MAILBOX_CACHE_UPDATE;
	else
		out->do_cache = 0;

	for (;;) {
		struct dirent *de;
		int seen;
		struct letter letter;
		struct maildir_cache_letter *cached;

		if ((de = readdir(mdir)) == NULL)
			break;
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;
		if ((seen = maildir_letter_seen(de->d_name)) == -1)
			goto fail;
		if (!view_seen && seen)
			continue;
		if (have_cache && (cached = maildir_cache_find(&cache, de->d_name)) != NULL) {
			letter.path = cached->path;
			letter.subject = cached->subject;
			letter.from = cached->from;
			letter.date = cached->date;

			cached->date = -1;
		}
		else {
			FILE *fp;

			if ((fp = fopenat(dfd, de->d_name)) == NULL) {
				warn("fopenat %s", de->d_name);
				goto fail;
			}

			if (letter_read(fp, &letter, &gl) == -1) {
				(void) fclose(fp);
				goto fail;
			}

			if (fclose(fp) == EOF) {
				goto fail;
			}
			if ((letter.path = strdup(de->d_name)) == NULL) {
				letter_free(&letter);
				goto fail;
			}
		}

		if (letter_push(&letter, out) == -1) {
			letter_free(&letter);
			goto fail;
		}
	}

	rv = 0;
	fail:
	free(gl.line);
	if (have_cache) {
		for (size_t i = 0; i < cache.nletters; i++) {
			/* has been taken */
			if (cache.letters[i].date == -1)
				continue;
			free(cache.letters[i].path);
			free(cache.letters[i].subject);
			free(cache.letters[i].from.str);
		}
		free(cache.letters);
	}
	if (rv != -1) {
		qsort(out->letters, out->nletters, sizeof(*out->letters),
			letter_cmp);
	}
	return rv;
}

void
mailbox_free(struct mailbox *mailbox)
{
	if (mailbox->do_cache == MAILBOX_CACHE_UPDATE 
			&& maildir_cache_write(mailbox) == -1) {
		warnx("maildir_cache");
	}
	for (long long i = 0; i < mailbox->nletters; i++)
		letter_free(&mailbox->letters[i]);
	free(mailbox->letters);
	(void) closedir(mailbox->cur);
}

static int
maildir_letter_seen(const char *name)
{
	char *flags;

	if ((flags = strstr(name, ":2,")) == NULL)
		return -1;
	flags += 3;
	return strchr(flags, 'S') != NULL;
}

int
mailbox_letter_mark_unread(struct mailbox *mailbox, struct letter *letter)
{
	return maildir_letter_unset_flag(mailbox->cur, letter, 'S');
}

int
mailbox_letter_mark_read(struct mailbox *mailbox, struct letter *letter)
{
	return maildir_letter_set_flag(mailbox->cur, letter, 'S');
}

static int
maildir_letter_unset_flag(DIR *dir, struct letter *letter, char f)
{
	char *path, name[NAME_MAX], *flags, *fl, *t;
	size_t len;
	int dfd;

	path = letter->path;

	if (strlcpy(name, path, sizeof(name)) >= sizeof(name))
		return -1;

	if ((flags = strchr(name, ':')) == NULL)
		return -1;
	/* not set, nothing to do */
	if ((fl = strchr(flags, f)) == NULL)
		return 0;

	/* fl[1] is valid because fl is NUL terminated */
	len = strlen(fl);
	(void) memmove(fl, &fl[1], len - 1);
	fl[len - 1] = '\0';

	dfd = dirfd(dir);

	if (renameat(dfd, path, dfd, name) == -1)
		return -1;

	/* need to dup since string has changed */
	t = strdup(name);
	if (t == NULL)
		return -1;
	free(path);
	letter->path = t;
	return 0;
}

static int
maildir_letter_set_flag(DIR *dir, struct letter *letter, char f)
{
	char name[NAME_MAX], *flags, *t, *path;
	int n, dfd;

	path = letter->path;

	if ((flags = strchr(path, ':')) == NULL)
		return -1;
	/* already set, nothing to do */
	if (strchr(flags, f) != NULL)
		return 0;

	/* append a flag */
	n = snprintf(name, NAME_MAX, "%s%c", path, f);

	if (n < 0 || n >= NAME_MAX)
		return -1;

	dfd = dirfd(dir);

	if (renameat(dfd, path, dfd, name) == -1)
		return -1;

	/* need to dup since string has changed */
	t = strdup(name);
	if (t == NULL)
		return -1;
	free(path);
	letter->path = t;
	return 0;
}

int
mailbox_letter_print(size_t nth, struct letter *letter)
{
	struct tm *tm;
	const char *subject;
	char date[30];
	struct from from;

	from_extract(&letter->from, &from);

	if ((tm = localtime(&letter->date)) == NULL
			|| strftime(date, sizeof(date), "%a %b %d %H:%M", tm) == 0)
		return -1;

	subject = letter->subject == NULL ? "No Subject" : letter->subject;

	if (printf("%4zu %-20s %-32.*s %-30s\n", nth, date, 
			from.al, from.addr, subject) < 0)
		return -1;
	return 0;
}

int
mailbox_print(struct mailbox *mailbox, size_t b, size_t e)
{
	for (; b < e; b++) {
		if (mailbox_letter_print(b + 1, &mailbox->letters[b]) == -1)
			return -1;
	}
	return 0;
}

static DIR *
opendirat(int at, const char *path)
{
	int fd;
	DIR *ret;

	if ((fd = openat(at, path, O_DIRECTORY | O_RDONLY | O_CLOEXEC)) == -1)
		return NULL;
	if ((ret = fdopendir(fd)) == NULL) {
		(void) close(fd);
	}
	return ret;
}

static FILE *
fopenat(int at, const char *path)
{
	FILE *ret;
	int fd;

	if ((fd = openat(at, path, O_RDONLY | O_CLOEXEC)) == -1)
		return NULL;
	if ((ret = fdopen(fd, "r")) == NULL) {
		(void) close(fd);
	}
	return ret;
}

void
letter_free(struct letter *letter)
{
	free(letter->subject);
	free(letter->from.str);
	free(letter->path);
}

static int
letter_push(struct letter *letter, struct mailbox *mailbox)
{
	void *t;

	/* should be impossible */
	if (mailbox->nletters == LLONG_MAX)
		return -1;

	t = reallocarray(mailbox->letters, mailbox->nletters + 1,
		sizeof(*mailbox->letters));
	if (t == NULL)
		return -1;
	mailbox->letters = t;
	mailbox->letters[mailbox->nletters] = *letter;
	mailbox->nletters++;

	return 0;
}

int
letter_read(FILE *fp, struct letter *letter, struct getline *gl)
{
	ssize_t len;

	letter->subject = NULL;
	letter->from.str = NULL;
	letter->date = -1;
	for (;;) {
		struct header header;

		if ((len = getline(&gl->line, &gl->n, fp)) == -1) {
			warnx("unexpected EOF");
			goto letter;
		}
		if (gl->line[len - 1] == '\n')
			gl->line[len - 1] = '\0';
		if (*gl->line == '\0')
			break;
		if (header_read(fp, gl, &header) == -1) {
			warnx("invalid header");
			goto letter;
		}
		if (header_push(&header, letter) == -1) {
			free(header.key);
			free(header.val);
			goto letter;
		}
	}

	if (ferror(fp))
		goto letter;

	if (letter->from.str == NULL || letter->date == -1) {
		warnx("letter missing From or Date headers");
		goto letter;
	}

	return 0;

	letter:
	free(letter->subject);
	free(letter->from.str);
	return -1;
}

static int
header_push(struct header *header, struct letter *letter)
{
	if (!strcasecmp(header->key, "Subject")) {
		if (letter->subject != NULL)
			return -1;
		letter->subject = header->val;
		free(header->key);
		return 0;
	}
	else if (!strcasecmp(header->key, "From")) {
		if (letter->from.str != NULL)
			return -1;
		/* takes ownership of header->val on success */
		if (from_safe_new(header->val, &letter->from) == -1)
			return -1;
		free(header->key);
		return 0;
	}
	else if (!strcasecmp(header->key, "Date")) {
		char *b;

		if (letter->date != -1)
			return -1;
		if ((b = strrchr(header->val, '(')) != NULL)
			*b = '\0';
		if ((letter->date = rfc5322_dateparse(header->val)) == -1) {
			warnx("letter has invalid Date header");
			return -1;
		}
		free(header->key);
		free(header->val);
		return 0;
	}
	else {
		free(header->key);
		free(header->val);
		return 0;
	}
}

static time_t
rfc5322_dateparse(const char *s)
{
	const char *fmt;
	struct tm tm;
	time_t rv;
	long off;

	if (strchr(s, ',') != NULL)
		fmt = "%a, %d %b %Y %H:%M:%S %z";
	else
		fmt = "%d %b %Y %H:%M:%S %z";

	memset(&tm, 0, sizeof(tm));
	if (strptime(s, fmt, &tm) == NULL)
		return -1;

	/* timegm mangles tm_gmtoff */
	off = tm.tm_gmtoff;
	if ((rv = timegm(&tm)) == -1)
		return -1;

	return rv - off;
}

static int
letter_cmp(const void *one, const void *two)
{
	const struct letter *n1 = one, *n2 = two;
	time_t u, d;

	u = n1->date;
	d = n2->date;
	if (u > d)
		return 1;
	else if (u == d)
		return 0;
	else
		return -1;
}

static char *
strip_trailing(char *p)
{
	char *e;

	e = &p[strlen(p) - 1];
	while (e > p && (*e == ' ' || *e == '\t'))
		e--;
	if (e != p)
		e++;
	*e = '\0';
	return e;
}

static int
header_read(FILE *fp, struct getline *gl, struct header *out)
{
	return header_read1(fp, gl, out, 1);
}

static int
header_read1(FILE *fp, struct getline *gl, struct header *out, int tv)
{
	size_t vlen;

	if ((out->val = strchr(gl->line, ':')) == NULL)
		return -1;
	*out->val++ = '\0';
	/* strip leading ws */
	out->val += strspn(out->val, " \t");
	if (tv)
		strip_trailing(out->val);

	out->key = gl->line;
	strip_trailing(out->key);

	for (size_t i = 0; out->key[i] != '\0'; i++) {
		if (!isprint( (unsigned char) out->key[i]))
			return -1;
	}

	for (vlen = 0; out->val[vlen] != '\0'; vlen++) {
		if (!isascii( (unsigned char) out->val[vlen]))
			return -1;
	}

	if ((out->key = strdup(out->key)) == NULL)
		return -1;
	if ((out->val = dupstr(out->val, vlen)) == NULL)
		goto key;

	for (;;) {
		char *line;
		int c;
		void *t;
		ssize_t len;
		char *e;

		if ((c = fgetc(fp)) == EOF)
			goto val;
		if (!isspace(c) || c == '\n') {
			if (fseek(fp, -1, SEEK_CUR) == -1)
				goto val;
			break;
		}

		if ((len = getline(&gl->line, &gl->n, fp)) == -1)
			goto val;
		if (tv && gl->line[len - 1] == '\n')
			gl->line[len - 1] = '\0';

		if (tv) {
			line = gl->line + strspn(gl->line, " \t");
			e = strip_trailing(line);
			len = e - line;
		}
		else
			line = gl->line;
		/* len is fine as is */

		for (size_t i = 0; line[i] != '\0'; i++) {
			if (!isascii( (unsigned char) line[i]))
				goto val;
		}

		t = realloc(out->val, vlen + len + 1);
		if (t == NULL)
			goto val;
		out->val = t;
		memcpy(&out->val[vlen], line, len);
		out->val[vlen + len] = '\0';
		vlen += len;
	}

	return 0;

	val:
	free(out->val);
	key:
	free(out->key);
	return -1;
}

static DIR *
maildir_setup(int dfd)
{
	DIR *cur, *new;
	struct dirent *de;
	char name[NAME_MAX];
	int n, curfd, newfd, rv;

	rv = -1;

	if ((cur = opendirat(dfd, "cur")) == NULL)
		return NULL;
	if ((new = opendirat(dfd, "new")) == NULL)
		goto cur;

	curfd = dirfd(cur);
	newfd = dirfd(new);

	while ((de = readdir(new)) != NULL) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		n =	snprintf(name, NAME_MAX, "%s:2,", de->d_name);
		if (n < 0 || n >= NAME_MAX)
			goto new;
		if (renameat(newfd, de->d_name, curfd, name) == -1)
			goto new;
	}

	rv = 0;
	new:
	if (closedir(new) == -1)
		rv = -1;
	cur:
	if (rv == -1) {
		(void) closedir(cur);
		cur = NULL;
	}
	return cur;
}

int
mailbox_letter_print_read(struct mailbox *mailbox, struct letter *letter,
	const struct options *options, FILE *out)
{
	struct headers headers;
	mbstate_t mbs;
	struct content_type ct;
	struct getline gl;
	FILE *fp;
	ssize_t len;
	struct header *h, *h2, f;
	char u8b[5];
	int dfd, c, qp, rv, utf8;

	rv = -1;

	dfd = dirfd(mailbox->cur);
	if ((fp = fopenat(dfd, letter->path)) == NULL)
		return -1;

	RB_INIT(&headers);

	gl.line = NULL;
	gl.n = 0;
	for (;;) {
		struct header header;

		if ((len = getline(&gl.line, &gl.n, fp)) == -1)
			goto headers;
		if (gl.line[len - 1] == '\n')
			gl.line[len - 1] = '\0';
		if (*gl.line == '\0')
			break;
		if (header_read1(fp, &gl, &header, 0) == -1)
			goto headers;
		if (header_push2(&header, &headers) == -1)
			goto headers;
	}

	for (size_t i = 0; i < options->nreorder; i++) {
		struct header *v;

		f.key = options->reorder[i];
		if ((v = RB_FIND(headers, &headers, &f)) == NULL)
			continue;
		if (!header_ignore(v, options)
				&& fprintf(out, "%s: %s\n", v->key, v->val) < 0)
			goto headers;
		(void) RB_REMOVE(headers, &headers, v);
		free(v->key);
		free(v->val);
		free(v);
	}

	RB_FOREACH(h, headers, &headers) {
		if (!header_ignore(h, options) 
				&& fprintf(out, "%s: %s\n", h->key, h->val) < 0)
			goto headers;
	}

	if (fputc('\n', out) == EOF)
		goto headers;

	f.key = "Content-Transfer-Encoding";
	qp = (h = RB_FIND(headers, &headers, &f)) != NULL 
		&& !strcasecmp(h->val, "quoted-printable");

	f.key = "Content-Type";
	utf8 = (h = RB_FIND(headers, &headers, &f)) != NULL
		&& content_type_parse(h->val, &ct) != -1
		&& ct.charset != NULL
		&& !strcasecmp(ct.charset, "utf-8");

	if (utf8) {
		memset(&mbs, 0, sizeof(mbs));
		u8b[0] = '\0';
	}

	while ((c = fgetc(fp)) != EOF) {
		if (c == '=' && (c = equal_escape(fp, qp)) == EOF)
			goto headers;

		if (utf8) {
			char cc;
			size_t len;

			cc = c;

			/* 
			 * prevent ESC type bytes from getting through...
			 */
			if (isascii(c) && !isprint(c) && !isspace(c))
				goto invalid;

			switch (mbrtowc(NULL, &cc, 1, &mbs)) {
			default:
				len = strlen(u8b);
				if (len == 4)
					goto invalid;
				u8b[len] = cc;

				if (fwrite(u8b, len + 1, 1, out) != len + 1)
					goto invalid;
				u8b[0] = '\0';
				break;
			case 0:
				goto invalid;
			case -1:
				goto invalid;
			case -2:
				len = strlen(u8b);
				if (len == 4)
					goto invalid;
				u8b[len] = cc;
				u8b[len + 1] = '\0';
				break;
			case -3:
				goto invalid;
			}

			continue;

			invalid:
			if (fputs("__[invalid]__", out) == EOF)
				goto headers;
			memset(&mbs, 0, sizeof(mbs));
		}
		else {
			if (!isprint(c) && !isspace(c)) {
				if (fputs("__[invalid]__", out) == EOF)
					goto headers;
			}
			else if (fputc(c, out) == EOF)
					goto headers;
		}
	}

	if (ferror(fp))
		goto headers;

	if (mailbox_letter_mark_read(mailbox, letter) == -1)
		goto headers;

	rv = 0;
	headers:
	RB_FOREACH_SAFE(h, headers, &headers, h2) {
		(void) RB_REMOVE(headers, &headers, h);
		free(h->key);
		free(h->val);
		free(h);
	}

	free(gl.line);
	if (fclose(fp) == EOF)
		rv = -1;
	return rv;
}

static int
header_cmp(struct header *one, struct header *two)
{
	return strcmp(one->key, two->key);
}

static int
header_ignore(struct header *header, const struct options *options)
{
	if (options->nunignore != 0) {
		for (size_t i = 0; i < options->nunignore; i++) {
			if (!strcasecmp(header->key, options->unignore[i]))
				return 0;
		}
		return 1;
	}
	else {
		for (size_t i = 0; i < options->nignore; i++) {
			if (!strcasecmp(header->key, options->ignore[i]))
				return 1;
		}
		return 0;
	}
}


static int
header_push2(struct header *header, struct headers *headers)
{
	struct header *fh, *hp;

	if ((fh = RB_FIND(headers, headers, header)) != NULL) {
		void *t;
		size_t len, len1;

		len = strlen(fh->val);
		len1 = strlen(header->val);

		t = realloc(fh->val, len + len1 + 1);
		if (t == NULL)
			goto header;
		fh->val = t;
		memcpy(&fh->val[len], header->val, len1);
		fh->val[len + len1] = '\0';

		free(header->key);
		free(header->val);
	}
	else {
		if ((hp = malloc(sizeof(*hp))) == NULL)
			goto header;
		*hp = *header;
		/* clang-tidy thinks that this can leak hp because RB_INSERT 
		 * will discard the inserted value if there already exists
		 * a value with that key. This cannot happen because RB_FIND 
		 * above has failed, meaning no value with this key exists.
		 * XXX: clean this up
		 */
		(void) RB_INSERT(headers, headers, hp); //NOLINT(clang-analyzer-unix.Malloc)
	}

	return 0;

	header:
	free(header->val);
	free(header->key);
	return -1;
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
		return '\n';

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

static char *
dupstr(const char *str, size_t n)
{
	char *rv;

	rv = malloc(n + 1);
	if (rv != NULL) {
		memcpy(rv, str, n);
		rv[n] = '\0';
	}
	return rv;
}

static int
maildir_cache_cmp(const void *one, const void *two)
{
	const struct letter *n1 = one, *n2 = two;

	return strcmp(n1->path, n2->path);
}

static int
maildir_cached_cmp(const void *one, const void *two)
{
	const char *n1 = one;
	const struct maildir_cache_letter *n2 = two;

	return strcmp(n1, n2->path);
}

static int
maildir_cache_write(struct mailbox *mailbox)
{
	char path[PATH_MAX];
	FILE *fp;
	uint32_t magic;
	uint8_t view_seen;
	int fd, n, rv;

	rv = -1;

	n = snprintf(path, sizeof(path), "%s/.mailzcache", mailbox->root);
	if (n < 0 || n >= sizeof(path))
		return -1;

	if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600)) == -1) {
		/* thats fine */
		if (errno == EACCES)
			return 0;
		return -1;
	}

	if (flock(fd, LOCK_EX) == -1) {
		(void) close(fd);
		return -1;
	}

	if ((fp = fdopen(fd, "w")) == NULL) {
		(void) close(fd);
		return -1;
	}

	magic = MAILDIR_CACHE_MAGIC | MAILDIR_CACHE_VERSION;
	if (fwrite(&magic, sizeof(magic), 1, fp) != 1)
		goto fail;
	
	view_seen = mailbox->view_seen;
	if (fwrite(&view_seen, sizeof(view_seen), 1, fp) != 1)
		goto fail;

	qsort(mailbox->letters, mailbox->nletters, sizeof(*mailbox->letters),
		maildir_cache_cmp);

	for (size_t i = 0; i < mailbox->nletters; i++) {
		uint64_t date;
		const struct letter *letter = &mailbox->letters[i];

		if (mailbox->letters[i].date > UINT64_MAX)
			goto fail;
		date = mailbox->letters[i].date;

		if (fwrite(&date, sizeof(date), 1, fp) != 1)
			goto fail;
		if (fwrite(letter->path, strlen(letter->path) + 1, 1, fp) != 1)
			goto fail;
		if (letter->subject != NULL) {
			if (fwrite(letter->subject, strlen(letter->subject) + 1, 1, fp) != 1)
				goto fail;
		}
		else if (fputc('\0', fp) == EOF)
			goto fail;
		if (fwrite(letter->from.str, strlen(letter->from.str) + 1, 1, fp) != 1)
			goto fail;
	}

	rv = 0;
	fail:
	if (flock(fileno(fp), LOCK_UN) == -1)
		rv = -1;
	if (fclose(fp) == EOF)
		rv = -1;
	return rv;
}

static int
maildir_cache_read(const char *root, struct maildir_cache *out)
{
	char path[PATH_MAX];
	struct stat sb;
	struct getline gl;
	FILE *fp;
	struct maildir_cache_letter *letters;
	size_t nletters;
	uint32_t magic;
	uint8_t view_seen;
	int n, rv, version;

	gl.line = NULL;
	gl.n = 0;
	rv = -1;

	n = snprintf(path, sizeof(path), "%s/.mailzcache", root);
	if (n < 0 || n >= sizeof(path))
		return -1;

	if ((fp = fopen(path, "r")) == NULL) {
		return -1;
	}

	if (flock(fileno(fp), LOCK_SH) == -1) {
		(void) fclose(fp);
		return -1;
	}

	letters = NULL;
	nletters = 0;

	if (fstat(fileno(fp), &sb) == -1)
		goto fail;

	if (fread(&magic, sizeof(magic), 1, fp) != 1)
		goto fail;
	/* accidental corruption */
	if ((magic & MAILDIR_CACHE_CHECK_MASK) != MAILDIR_CACHE_MAGIC)
		goto fail;
	version = (magic & MAILDIR_CACHE_VERSION_MASK);

	if (version != MAILDIR_CACHE_VERSION)
		goto fail;

	if (fread(&view_seen, sizeof(view_seen), 1, fp) != 1)
		goto fail;

	for (;;) {
		struct maildir_cache_letter letter, *t;
		size_t sn;
		uint64_t date;
		char *from;

		sn = fread(&date, 1, sizeof(date), fp);
		if (sn == 0) /* EOF (maybe) */
			break;
		if (sn != sizeof(date))
			goto fail;
		letter.date = date;

		if (getdelim(&gl.line, &gl.n, '\0', fp) == -1)
			goto fail;
		if ((letter.path = strdup(gl.line)) == NULL)
			goto fail;

		if (getdelim(&gl.line, &gl.n, '\0', fp) == -1) {
			free(letter.path);
			goto fail;
		}
		if (*gl.line == '\0') /* empty */
			letter.subject = NULL;
		else if ((letter.subject = strdup(gl.line)) == NULL) {
			free(letter.path);
			goto fail;
		}

		if (getdelim(&gl.line, &gl.n, '\0', fp) == -1) {
			free(letter.path);
			free(letter.subject);
			goto fail;
		}
		if ((from = strdup(gl.line)) == NULL) {
			free(letter.path);
			free(letter.subject);
			goto fail;
		}
		if (from_safe_new(from, &letter.from) == -1) {
			free(letter.path);
			free(letter.subject);
			free(from);
			goto fail;
		}

		t = reallocarray(letters, nletters + 1, sizeof(*letters));
		if (t == NULL) {
			free(letter.path);
			free(letter.subject);
			free(letter.from.str);
			goto fail;
		}
		letters = t;
		letters[nletters++] = letter;
	}

	if (ferror(fp))
		goto fail;

	rv = 0;
	fail:
	free(gl.line);
	if (flock(fileno(fp), LOCK_UN) == -1)
		rv = -1;
	if (fclose(fp) == EOF)
		rv = -1;
	if (rv == -1) {
		for (size_t i = 0; i < nletters; i++) {
			free(letters[i].subject);
			free(letters[i].from.str);
		}
		free(letters);
	}
	else {
		out->letters = letters;
		out->nletters = nletters;
		out->mtim = sb.st_mtim.tv_sec;
		out->view_seen = view_seen;
	}
	return rv;
}

static struct maildir_cache_letter *
maildir_cache_find(struct maildir_cache *cache, const char *path)
{
	return bsearch(path, cache->letters, cache->nletters, sizeof(*cache->letters),
		maildir_cached_cmp);
}

int
mailbox_letter_print_content(struct mailbox *mailbox,
	struct letter *letter, FILE *out)
{
	struct getline gl;
	FILE *fp;
	int c, dfd, lastnl, rv;
	struct letter tl;
	struct tm *tm;
	char date[33];
	struct from from;

	rv = -1;

	if ((tm = localtime(&letter->date)) == NULL)
		return -1;
	if (strftime(date, sizeof(date), "%a, %b %e, %Y at %H:%M %z", tm) == 0)
		return -1;
	from_extract(&letter->from, &from);

	dfd = dirfd(mailbox->cur);
	if ((fp = fopenat(dfd, letter->path)) == NULL)
		return -1;

	gl.line = NULL;
	gl.n = 0;
	/* advances file pointer to just past the letter content */
	if (letter_read(fp, &tl, &gl) == -1) {
		free(gl.line);
		goto fail;
	}
	free(tl.subject);
	free(tl.from.str);
	free(gl.line);

	if (fprintf(out, "On %s, %.*s wrote:\n", date, from.nl, from.name) < 0)
		goto fail;

	if (fputs("> ", out) == EOF)
		goto fail;
	lastnl = 0;
	while ((c = fgetc(fp)) != EOF) {
		if (lastnl) {
			if (fputs("> ", out) == EOF)
				goto fail;
			lastnl = 0;
		}

		if (fputc(c, out) == EOF)
			goto fail;
		if (c == '\n')
			lastnl = 1;
	}

	if (ferror(fp))
		goto fail;

	rv = 0;
	fail:
	if (fclose(fp) == EOF)
		rv = -1;
	return rv;
}

static int
content_type_parse(char *s, struct content_type *out)
{
	char *charset, *param, *subtype, *type, *tst;

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

		if (*key++ != ' ')
			return -1;

		if ((val = param) == NULL)
			return -1;

		len = strlen(val);
		if (val[0] == '\"' && val[len - 1] == '\"') {
			val[len - 1] = '\0';
			val++;
		}
		if (!strcasecmp(key, "charset"))
			charset = val;
	}

	if (out != NULL) {
		out->charset = charset;
		out->subtype = subtype;
		out->type = type;
	}

	return 0;
}

int
letter_test(void)
{
	FILE *fp;
	struct letter letter;
	struct getline gl;
	int rv;

	rv = 1;
	if (pledge("stdio rpath", NULL) == -1)
		err(1, "pledge");
	if ((fp = fopen("tests/letter", "r")) == NULL)
		err(1, "fopen");
	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	gl.line = NULL;
	gl.n = 0;
	if (letter_read(fp, &letter, &gl) == -1) {
		free(gl.line);
		fclose(fp);
		return 1;
	}

	if (strcmp(letter.from.str, "A friend <gary@nota.realdomain>") != 0)
		goto fail;
	if (letter.subject == NULL || strcmp(letter.subject, "Test mail") != 0)
		goto fail;
	if (letter.date != 1718918773)
		goto fail;

	rv = 0;
	fail:
	free(letter.subject);
	free(letter.from.str);
	(void) fclose(fp);
	free(gl.line);

	return rv;
}
