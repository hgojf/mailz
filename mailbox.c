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
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "date.h"
#include "mail.h"
#include "mailbox.h"
#include "reallocarray.h"

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
static int fwriteat(FILE *, const char *);
static DIR *opendirat(int, const char *);

static int header_ignore(struct header *, const struct options *);
static int header_push(struct header *, struct letter *);
static int header_push2(struct header *, struct headers *);
static int header_read(FILE *, char **, size_t *, struct header *);

static int letter_cmp(const void *, const void *);
static int letter_push(struct letter *, struct mailbox *);

static DIR *maildir_setup(int);
static int maildir_letter_set_flag(DIR *, struct letter *, char);
static int maildir_letter_seen(const char *);

static int mbox_flush(struct mailbox *);
static int mbox_letter_cmp(const void *, const void *);
static int mbox_rejig(struct mailbox *, size_t);

static char *dupstr(const char *, size_t);
static char *strip_trailing(char *);

/* function takes ownership of 'fd', closing on failure */
int
mailbox_setup(const char *path, struct mailbox *out)
{
	int type, rv, fd;
	struct stat sb;

	rv = -1;
	if (stat(path, &sb) == -1)
		return -1;
	switch (sb.st_mode & S_IFMT) {
	case S_IFDIR:
		type = MAILBOX_MAILDIR;
		if ((fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) == -1)
			return -1;
		break;
	case S_IFREG:
		type = MAILBOX_MBOX;
		if ((fd = open(path, O_RDWR | O_CLOEXEC)) == -1)
			return -1;
		break;
	default:
		return -1;
	}

	if (type == MAILBOX_MAILDIR) {
		DIR *dp;

		if ((dp = maildir_setup(fd)) == NULL)
			goto fail;
		out->val.maildir_cur = dp;
		if (close(fd) == -1)
			return -1;
	}
	else {
		FILE *fp;

		if ((fp = fdopen(fd, "a+")) == NULL)
			goto fail;
		out->val.mbox_file = fp;
	}

	out->type = type;
	rv = 0;
	fail:
	if (rv == -1)
		(void) close(fd);
	return rv;
}

int
mailbox_read(struct mailbox *out, int view_seen)
{
	FILE *fp, *mbox;
	DIR *mdir;
	int dfd, rv, type;

	rv = -1;

	type = out->type;
	assert(type == MAILBOX_MBOX || type == MAILBOX_MAILDIR);

	if (type == MAILBOX_MBOX)
		fp = mbox = out->val.mbox_file;
	else if (type == MAILBOX_MAILDIR) {
		mdir = out->val.maildir_cur;
		dfd = dirfd(mdir);
		fp = NULL;
	}

	out->letters = NULL;
	out->nletters = 0;

	/* first message has 'From' on the first line */
	if (type == MAILBOX_MBOX) {
		char from[4];
		int c, seen;
		size_t n;
		struct letter letter;
		long off;

		for (;;) {
			if ((c = fgetc(fp)) == EOF)
				goto fail;
			if (c != '\n') {
				if (fseek(fp, -1, SEEK_CUR) == -1)
					goto fail;
				break;
			}
		}

		if ((n = fread(from, 1, 4, fp)) == 0 || n != 4)
			goto fail;
		if (memcmp(from, "From", 4) != 0)
			goto fail;
		for (;;) {
			if ((c = fgetc(fp)) == EOF)
				goto fail;
			if (c == '\n')
				break;
		}

		if ((off = ftell(fp)) == -1)
			goto fail;
		if (letter_read(fp, &letter, type, &seen) == -1)
			goto fail;
		letter.ident.mbox.offset = off;
		letter.ident.mbox.seen = 0;

		if (!view_seen && seen) {
			letter_free(type, &letter);
		}
		else if (letter_push(&letter, out) == -1) {
			letter_free(type, &letter);
			goto fail;
		}
	}

	for (;;) {
		struct dirent *de;
		long off;
		int seen;
		struct letter letter;

		if (type == MAILBOX_MAILDIR) {
			int seen;

			if ((de = readdir(mdir)) == NULL)
				break;
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
				continue;
			if ((seen = maildir_letter_seen(de->d_name)) == -1)
				goto fail;
			if (!view_seen && seen)
				continue;
			if ((fp = fopenat(dfd, de->d_name)) == NULL)
				goto fail;
		}

		if (type == MAILBOX_MBOX) {
			int c;

			/* find next 'From' line */
			for (;;) {
				char from[4];
				size_t n;

				if ((c = fgetc(fp)) == EOF)
					goto done;
				if (c != '\n')
					continue;
				if ((n = fread(from, 1, 4, fp)) == 0)
					goto done;
				if (n == 4 && memcmp(from, "From", 4) == 0)
					break;
				if (fseek(fp, - (long) n, SEEK_CUR) == -1)
					goto fail;
			}
			for (;;) {
				if ((c = fgetc(fp)) == EOF)
					goto fail;
				if (c == '\n')
					break;
			}
			if ((off = ftell(fp)) == -1)
				goto fail;
		}

		if (letter_read(fp, &letter, type, &seen) == -1)
			goto fail;

		if (type == MAILBOX_MAILDIR) {
			/* obviously this is weird, because it will 'try again' */
			if (fclose(fp) == EOF)
				goto fail;
			if ((letter.ident.maildir_path = strdup(de->d_name)) == NULL) {
				letter_free(type, &letter);
				goto fail;
			}
			fp = NULL;
		}

		if (type == MAILBOX_MBOX) {
			letter.ident.mbox.offset = off;
			letter.ident.mbox.seen = 0;
		}

		if (!view_seen && seen) {
			letter_free(type, &letter);
			continue;
		}

		if (letter_push(&letter, out) == -1) {
			letter_free(type, &letter);
			goto fail;
		}
	}
	done:

	rv = 0;
	fail:
	if (type == MAILBOX_MAILDIR && fp != NULL) {
		if (fclose(fp) == EOF)
			rv = -1;
	}
	if (rv == -1) {
		for (long long i = 0; i < out->nletters; i++)
			letter_free(type, &out->letters[i]);
		free(out->letters);
		if (type == MAILBOX_MAILDIR)
			(void) closedir(mdir);
		else if (type == MAILBOX_MBOX)
			(void) fclose(mbox);
	}
	else {
		qsort(out->letters, out->nletters, sizeof(*out->letters),
			letter_cmp);
	}
	return rv;
}

int
mailbox_close(struct mailbox *mailbox)
{
	if (mailbox->type == MAILBOX_MBOX)
		return mbox_flush(mailbox);
	return 0;
}

void
mailbox_free(struct mailbox *mailbox)
{
	if (mailbox->type == MAILBOX_MBOX) {
		(void) mbox_flush(mailbox);
		(void) fclose(mailbox->val.mbox_file);
	}
	for (long long i = 0; i < mailbox->nletters; i++)
		letter_free(mailbox->type, &mailbox->letters[i]);
	free(mailbox->letters);
	if (mailbox->type == MAILBOX_MAILDIR)
		(void) closedir(mailbox->val.maildir_cur);
}

static int
maildir_letter_seen(const char *name)
{
	char *flags, flag;
	int n;

	/* shouldnt have made it this far */
	if ((flags = strchr(name, ':')) == NULL)
		return -1;
	flags++;
	n = sscanf(flags, "2,%c", &flag);
	return n == 1 && flag == 'S';
}

int
mailbox_letter_mark_unread(struct mailbox *mailbox, struct letter *letter)
{
	if (mailbox->type == MAILBOX_MAILDIR) {
		if (maildir_letter_set_flag(mailbox->val.maildir_cur, letter, '\0') == -1)
			return -1;
	}
	else if (mailbox->type == MAILBOX_MBOX)
		letter->ident.mbox.seen = 0;
	return 0;
}

int
mailbox_letter_mark_read(struct mailbox *mailbox, struct letter *letter)
{
	if (mailbox->type == MAILBOX_MAILDIR) {
		if (maildir_letter_set_flag(mailbox->val.maildir_cur, letter, 'S') == -1)
			return -1;
	}
	else if (mailbox->type == MAILBOX_MBOX)
		letter->ident.mbox.seen = 1;
	return 0;
}

static int
maildir_letter_set_flag(DIR *dir, struct letter *letter, char f)
{
	char name[NAME_MAX], *flags, *t, *path;
	int n, dfd;

	path = letter->ident.maildir_path;

	/* remove existing flags, if they exist */
	if ((flags = strchr(path, ':')) == NULL)
		return -1;
	*flags = '\0';

	/* hide flags for this call */
	n = snprintf(name, NAME_MAX, "%s:2,%c", path, f);

	/* now unhide */
	*flags = ':';

	if (n < 0 || n >= NAME_MAX)
		return -1;


	dfd = dirfd(dir);

	if (renameat(dfd, path, dfd, name) == -1)
		return -1;

	/* need to dup in case flags changed, could be more selective */
	t = strdup(name);
	if (t == NULL)
		return -1;
	free(path);
	letter->ident.maildir_path = t;
	return 0;
}


int
mailbox_letter_print(size_t nth, struct letter *letter)
{
	struct tm *tm;
	char date[30];
	struct from from;

	if (from_extract(letter->from, &from) == -1)
		return -1;

	if ((tm = localtime(&letter->date)) == NULL
			|| strftime(date, sizeof(date), "%a %b %d %H:%M", tm) == 0)
		return -1;

	if (printf("%4zu %-20s %-32.*s %-30s\n", nth, date, 
		from.al, from.addr,
		letter->subject == NULL ? "No Subject" : letter->subject) < 0)
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
letter_free(int type, struct letter *letter)
{
	free(letter->subject);
	free(letter->from);
	if (type == MAILBOX_MAILDIR)
		free(letter->ident.maildir_path);
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
letter_read(FILE *fp, struct letter *letter, int type, int *seen)
{
	char *line = NULL;
	size_t n = 0;
	ssize_t len;

	letter->subject = NULL;
	letter->from = NULL;
	letter->date = -1;
	*seen = 0;
	for (;;) {
		struct header header;

		if ((len = getline(&line, &n, fp)) == -1)
			goto letter;
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		if (*line == '\0')
			break;
		if (header_read(fp, &line, &n, &header) == -1)
			goto letter;
		if (type == MAILBOX_MBOX && strcmp(header.key, "Status") == 0) {
			if (strchr(header.val, 'R') != NULL)
				*seen = 1;
		}
		if (header_push(&header, letter) == -1) {
			free(header.key);
			free(header.val);
			goto letter;
		}
	}

	if (letter->from == NULL || letter->date == -1)
		goto letter;

	free(line);
	return 0;

	letter:
	free(letter->subject);
	free(letter->from);

	free(line);
	return -1;
}

int
from_extract(char *from, struct from *out)
{
	char *m;
	size_t al, nl;

	if ((m = strchr(from, '<')) != NULL) {
		al = strlen(m) - 2;
		if (al > INT_MAX)
			return -1;
		nl = (m - from);
		if (m != from) /* addresses in the form '<addr>' */
			nl -= 1;
		if (nl > INT_MAX)
			return -1;

		if (m[al + 1] != '>')
			return -1;

		out->addr = m + 1;
		out->al = al;
		out->name = from;
		out->nl = nl;
	}
	else {
		al = strlen(from);
		if (al > INT_MAX)
			return -1;

		out->addr = from;
		out->al = al;
		out->nl = 0;
		out->name = NULL;
	}

	return 0;
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
		if (letter->from != NULL)
			return -1;
		/* takes ownership of header->val on success */
		letter->from = header->val;
		free(header->key);
		return 0;
	}
	else if (!strcasecmp(header->key, "Date")) {
		struct tm tm;
		char *tz, *b;
		time_t off;
		const char *fmt;

		if (letter->date != -1)
			return -1;

		/* dont yet support ignoring comments except in this common case */
		if ((b = strrchr(header->val, '(')) != NULL)
			*b = '\0';
		if ((tz = strrchr(header->val, ' ')) == NULL)
			return -1;
		*tz++ = '\0';
		if ((off = tz_tosec(tz)) == TZ_INVALIDSEC)
			return -1;
		memset(&tm, 0, sizeof(tm));
		if (strchr(header->val, ',') != NULL)
			fmt = "%a, %d %b %Y %H:%M:%S";
		else
			fmt = "%d %b %Y %H:%M:%S";
		if (strptime(header->val, fmt, &tm) == NULL)
			return -1;
		if ((letter->date = mktime(&tm)) == -1)
			return -1;
		if (letter->date < off)
			return -1;
		letter->date -= off;
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
header_read(FILE *fp, char **lp, size_t *np, struct header *out)
{
	size_t vlen;

	if ((out->val = strchr(*lp, ':')) == NULL)
		return -1;
	*out->val++ = '\0';
	/* strip leading ws */
	out->val += strspn(out->val, " \t");
	(void) strip_trailing(out->val);

	out->key = *lp;
	(void) strip_trailing(out->val);

	for (size_t i = 0; out->key[i] != '\0'; i++) {
		if (!isprint(out->key[i]))
			return -1;
	}

	for (vlen = 0; out->val[vlen] != '\0'; vlen++) {
		if (!isascii(out->val[vlen]))
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

		if ((len = getline(lp, np, fp)) == -1)
			goto val;
		if ((*lp)[len - 1] == '\n') {
			(*lp)[len - 1] = '\0';
			len--;
		}

		line = (*lp) + strspn(*lp, " \t");
		e = strip_trailing(line);
		len = e - line;

		for (size_t i = 0; line[i] != '\0'; i++) {
			if (!isascii(line[i]))
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
	FILE *fp;
	char *line;
	size_t lastnl, n;
	ssize_t len;
	struct header *h, *h2;
	int c, rv;

	rv = -1;
	assert(mailbox->type == MAILBOX_MBOX || mailbox->type == MAILBOX_MAILDIR);
	assert(options->linewrap >= 0);

	if (mailbox->type == MAILBOX_MBOX) {
		fp = mailbox->val.mbox_file;
		if (fseek(fp, letter->ident.mbox.offset, SEEK_SET) == -1)
			return -1;
	}
	if (mailbox->type == MAILBOX_MAILDIR) {
		int dfd = dirfd(mailbox->val.maildir_cur);
		if ((fp = fopenat(dfd, letter->ident.maildir_path)) == NULL)
			return -1;
	}

	RB_INIT(&headers);

	line = NULL;
	n = 0;
	for (;;) {
		struct header header;

		if ((len = getline(&line, &n, fp)) == -1)
			goto headers;
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		if (*line == '\0')
			break;
		if (header_read(fp, &line, &n, &header) == -1)
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

	lastnl = 0;
	while ((c = fgetc(fp)) != EOF) {
		if (c == '=' && (c = equal_escape(fp)) == EOF)
			goto headers;
		if (!isprint(c) && !isspace(c)) {
			if (fputs("__[invalid]__", out) == EOF)
				goto headers;
		}
		else if (fputc(c, out) == EOF)
				goto headers;

		if (c == '\n')
			lastnl = 0;
		else if (options->linewrap != 0 && lastnl == options->linewrap) {
			if (fputc('\n', out) == EOF)
				goto headers;
			lastnl = 0;
		}
		else
			lastnl++;

		if (c == '\n' && mailbox->type == MAILBOX_MBOX) {
			/* Find next 'From' line */
			char from[4];
			size_t n;

			if ((n = fread(from, 1, 4, fp)) == 0)
				break;
			if (n == 4 && memcmp(from, "From", 4) == 0)
				break;
			/* allow these characters to be dealth with as normal */
			if (fseek(fp, - (long) n, SEEK_CUR) == -1)
				goto headers;
		}
	}

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

	free(line);
	if (mailbox->type == MAILBOX_MAILDIR && fclose(fp) == EOF)
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
	FILE *fp;
	int c, type, rv;
	struct letter tl;
	struct tm *tm;
	char date[33];
	struct from from;
	int lastnl, seen;

	rv = -1;

	type = mailbox->type;
	assert(type == MAILBOX_MBOX || type == MAILBOX_MAILDIR);

	if ((tm = localtime(&letter->date)) == NULL)
		return -1;
	if (strftime(date, sizeof(date), "%a, %b %e, %Y at %H:%M %z", tm) == 0)
		return -1;
	if (from_extract(letter->from, &from) == -1)
		return -1;

	if (type == MAILBOX_MBOX) {
		fp = mailbox->val.mbox_file;
		if (fseek(fp, letter->ident.mbox.offset, SEEK_SET) == -1)
			return -1;
	}
	if (type == MAILBOX_MAILDIR) {
		int dfd;

		dfd = dirfd(mailbox->val.maildir_cur);
		if ((fp = fopenat(dfd, letter->ident.maildir_path)) == NULL)
			return -1;
	}

	/* advances file pointer to just past the letter content */
	if (letter_read(fp, &tl, type, &seen) == -1)
		goto fail;
	free(tl.subject);
	free(tl.from);

	if (fprintf(out, "On %s, %.*s wrote:\n", date, from.nl, from.name) < 0)
		goto fail;

	if (fputs("> ", out) == EOF)
		goto fail;
	lastnl = 0;
	while ((c = fgetc(fp)) != EOF) {
		/* find next 'From' line */
		if (type == MAILBOX_MBOX && c == '\n') {
			char from[4];
			size_t n;

			if ((n = fread(from, 1, 4, fp)) == 0)
				goto done;
			if (memcmp(from, "From", 4) == 0)
				break;
			if (fseek(fp, - (long) n, SEEK_CUR) == -1)
				goto fail;
		}

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
	done:

	if (ferror(fp))
		goto fail;

	rv = 0;
	fail:
	if (type == MAILBOX_MAILDIR) {
		if (fclose(fp) == EOF)
			rv = -1;
	}
	return rv;
}

/* 
 * flushes changes to mbox file, destructive to mailbox,
 * as it reorders the letters, and invalidates all offsets
 */
static int
mbox_flush(struct mailbox *mailbox)
{
	qsort(mailbox->letters, mailbox->nletters,
		sizeof(*mailbox->letters), mbox_letter_cmp);
	for (long long i = 0; i < mailbox->nletters; i++) {
		if (!mailbox->letters[i].ident.mbox.seen)
			continue;
		if (mbox_rejig(mailbox, i) == -1)
			return -1;
	}
	return 0;
}

static int
mbox_rejig(struct mailbox *mailbox, size_t idx)
{
	struct letter *letter; 
	FILE *fp;
	char *line;
	size_t n;
	int rv;

	fp = mailbox->val.mbox_file;
	letter = &mailbox->letters[idx];
	rv = -1;

	if (fseek(fp, letter->ident.mbox.offset, SEEK_SET) == -1)
		return -1;

	line = NULL;
	n = 0;
	for (;;) {
		struct header header;
		ssize_t len;
		int same, hr;

		if ((len = getline(&line, &n, fp)) == -1)
			goto fail;
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		if (*line == '\0')
			break;
		if (header_read(fp, &line, &n, &header) == -1)
			goto fail;

		same = strcmp(header.key, "Status") == 0;
		hr = strchr(header.val, 'R') != NULL;

		free(header.key);
		free(header.val);

		if (same) {
			/* Status header already present with 'R', nothing to do. */
			if (hr) {
				free(line);
				return 0;
			}
			/* Append 'R' to the header */
			if (fseek(fp, -1, SEEK_CUR) == -1)
				goto fail;
			if (fwriteat(fp, "R") == -1)
				goto fail;
			free(line);
			return 0;
		}
	}

	if (fseek(fp, -1, SEEK_CUR) == -1)
		goto fail;
	if (fwriteat(fp, "Status: R\n") == -1)
		goto fail;

	rv = 0;
	fail:
	free(line);
	return rv;
}

static int
fwriteat(FILE *fp, const char *str)
{
	int ch, tfd, fd, rv;
	long off;
	FILE *cp;
	char path[] = "/tmp/mail/flush.XXXXXX";

	rv = -1;

	if ((fd = mkstemp(path)) == -1)
		return -1;
	if (unlink(path) == -1) {
		(void) close(fd);
		return -1;
	}
	if ((cp = fdopen(fd, "a+")) == NULL) {
		(void) close(fd);
		return -1;
	}

	if ((off = ftell(fp)) == -1)
		goto fail;
	while ((ch = fgetc(fp)) != EOF) {
		if (fputc(ch, cp) == EOF)
			goto fail;
	}
	if (ferror(fp))
		goto fail;
	if (fseek(cp, 0, SEEK_SET) == -1)
		goto fail;
	if (fseek(fp, off, SEEK_SET) == -1)
		goto fail;
	if ((tfd = fileno(fp)) == -1)
		goto fail;
	if (ftruncate(tfd, off) == -1)
		goto fail;

	if (fputs(str, fp) == EOF)
		goto fail;
	while ((ch = fgetc(cp)) != EOF) {
		if (fputc(ch, fp) == EOF)
			goto fail;
	}

	rv = 0;
	fail:
	if (fclose(fp) == -1)
		rv = -1;
	return rv;
}

/* descending order based on offset */
static int
mbox_letter_cmp(const void *one, const void *two)
{
	const struct letter *n1 = one, *n2 = two;
	long v1, v2;

	return 0;
	v1 = n1->ident.mbox.offset;
	v2 = n2->ident.mbox.offset;

	if (v1 < v2)
		return 1;
	else if (v1 == v2)
		return 0;
	else
		return -1;
}
