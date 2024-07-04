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
#include "config.h"

#include <sys/stat.h>
#ifdef HAVE_SYS_TREE_H
#include <sys/tree.h>
#else
#include "tree.h"
#endif /* HAVE_SYS_TREE_H */

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "address.h"
#include "date.h"
#include "mail.h"
#include "mailbox.h"
#include "reallocarray.h"
#include "strlcpy.h"

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

static int equal_escape(FILE *);

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

static DIR *maildir_setup(int);
static int maildir_letter_set_flag(DIR *, struct letter *, char);
static int maildir_letter_unset_flag(DIR *, struct letter *, char);
static int maildir_letter_seen(const char *);

static char *dupstr(const char *, size_t);
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

	return 0;
}

int
mailbox_read(struct mailbox *out, int view_seen)
{
	struct getline gl;
	FILE *fp;
	DIR *mdir;
	int dfd, rv;

	rv = -1;

	mdir = out->cur;
	dfd = dirfd(mdir);
	fp = NULL;

	out->letters = NULL;
	out->nletters = 0;

	gl.line = NULL;
	gl.n = 0;

	for (;;) {
		struct dirent *de;
		int seen;
		struct letter letter;

		if ((de = readdir(mdir)) == NULL)
			break;
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;
		if ((seen = maildir_letter_seen(de->d_name)) == -1)
			goto fail;
		if (!view_seen && seen)
			continue;
		if ((fp = fopenat(dfd, de->d_name)) == NULL) {
			warn("fopenat %s", de->d_name);
			goto fail;
		}

		if (letter_read(fp, &letter, &gl) == -1)
			goto fail;

		if (fclose(fp) == EOF) {
			fp = NULL;
			goto fail;
		}
		fp = NULL;
		if ((letter.path = strdup(de->d_name)) == NULL) {
			letter_free(&letter);
			goto fail;
		}

		if (letter_push(&letter, out) == -1) {
			letter_free(&letter);
			goto fail;
		}
	}

	rv = 0;
	fail:
	free(gl.line);
	if (fp != NULL && fclose(fp) == EOF)
		rv = -1;
	if (rv != -1) {
		qsort(out->letters, out->nletters, sizeof(*out->letters),
			letter_cmp);
	}
	return rv;
}

void
mailbox_free(struct mailbox *mailbox)
{
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
	struct getline gl;
	FILE *fp;
	ssize_t len;
	struct header *h, *h2;
	int dfd, c, rv;

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
		struct header *v, f;

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

	while ((c = fgetc(fp)) != EOF) {
		if (c == '=' && (c = equal_escape(fp)) == EOF)
			goto headers;
		if (!isprint(c) && !isspace(c)) {
			if (fputs("__[invalid]__", out) == EOF)
				goto headers;
		}
		else if (fputc(c, out) == EOF)
				goto headers;
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
equal_escape(FILE *fp)
{
	int t;

	if ((t = fgetc(fp)) == EOF)
		return '=';
	if (t == '\n')
		return '\n';
	if (fseek(fp, -1, SEEK_CUR) == -1)
		return EOF;
	return '=';
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
	if (letter.date != 1718936773)
		goto fail;

	rv = 0;
	fail:
	free(letter.subject);
	free(letter.from.str);
	(void) fclose(fp);
	free(gl.line);

	return rv;
}
