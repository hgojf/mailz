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
static DIR *opendirat(int, const char *);

static int header_push(struct header *, struct letter *);
static int header_push2(struct header *, struct headers *);
static int header_read(FILE *, char **, size_t *, struct header *);
static int header_ignore(struct header *, const struct options *);
static int push_letter(int, struct letter *, struct mailbox *);
static int read_letter(FILE *, struct letter *);

static void letter_free(int, struct letter *);
static int letter_cmp(const void *, const void *);

static DIR *maildir_setup(int);
static int maildir_letter_set_flag(DIR *, struct letter *, char);
static int maildir_letter_seen(const char *);

int
mailbox_setup(int fd, struct mailbox *out)
{
	int type;
	struct stat sb;

	if (fstat(fd, &sb) == -1)
		return -1;
	switch (sb.st_mode & S_IFMT) {
	case S_IFDIR:
		type = MAILBOX_MAILDIR;
		break;
	case S_IFREG:
		type = MAILBOX_MBOX;
		break;
	default:
		return -1;
	}

	if (type == MAILBOX_MAILDIR) {
		DIR *dp;

		if ((dp = maildir_setup(fd)) == NULL)
			return -1;
		out->val.maildir_cur = dp;
	}
	else {
		FILE *fp;

		if ((fp = fdopen(fd, "r")) == NULL)
			return -1;
		out->val.mbox_file = fp;
	}
	out->type = type;
	return 0;
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
	for (;;) {
		struct dirent *de;
		long off;
		struct letter letter;

		if (type == MAILBOX_MAILDIR) {
			if ((de = readdir(mdir)) == NULL)
				break;
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
				continue;
			if (!view_seen && maildir_letter_seen(de->d_name))
				continue;
			if ((fp = fopenat(dfd, de->d_name)) == NULL)
				goto fail;
		}

		if (type == MAILBOX_MBOX) {
			char rom[3];
			int c, n;

			/* find next 'From' line */
			for (;;) {
				for (;;) {
					if ((c = fgetc(fp)) == EOF)
						goto done;
					if (c == 'F')
						break;
				}

				if ((n = fread(rom, 1, 3, fp)) == EOF || n != 3)
					goto done;
				if (memcmp(rom, "rom", 3) == 0)
					break;
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

		if (read_letter(fp, &letter) == -1)
			goto fail;

		if (type == MAILBOX_MAILDIR) {
			if ((letter.ident.maildir_path = strdup(de->d_name)) == NULL) {
				letter_free(type, &letter);
				goto fail;
			}
			fclose(fp);
			fp = NULL;
		}

		if (type == MAILBOX_MBOX)
			letter.ident.mbox_offset = off;

		if (push_letter(type, &letter, out) == -1)
			goto fail;
	}
	done:

	rv = 0;
	fail:
	if (type == MAILBOX_MAILDIR && fp != NULL)
		fclose(fp);
	if (rv == -1) {
		for (long long i = 0; i < out->nletters; i++)
			letter_free(type, &out->letters[i]);
		free(out->letters);
		if (type == MAILBOX_MAILDIR)
			closedir(mdir);
		else if (type == MAILBOX_MBOX)
			fclose(mbox);
	}
	else {
		qsort(out->letters, out->nletters, sizeof(*out->letters),
			letter_cmp);
	}
	return rv;
}

void
mailbox_free(struct mailbox *mailbox)
{
	for (long long i = 0; i < mailbox->nletters; i++)
		letter_free(mailbox->type, &mailbox->letters[i]);
	free(mailbox->letters);
	if (mailbox->type == MAILBOX_MAILDIR)
		closedir(mailbox->val.maildir_cur);
	else if (mailbox->type == MAILBOX_MBOX)
		fclose(mailbox->val.mbox_file);
}

static int
maildir_letter_seen(const char *name)
{
	char *flags, flag;
	int n;

	if ((flags = strchr(name, ':')) == NULL)
		return -1;
	flags++;
	n = sscanf(flags, "2,%c", &flag);
	return n != EOF && flag == 'S';
}

int
mailbox_letter_mark_unread(struct mailbox *mailbox, struct letter *letter)
{
	if (mailbox->type == MAILBOX_MAILDIR) {
		if (maildir_letter_set_flag(mailbox->val.maildir_cur, letter, '\0') == -1)
			return -1;
	}
	else if (mailbox->type == MAILBOX_MBOX)
		; /* STUB */
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
		; /* STUB */
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

	if ((tm = localtime(&letter->date)) == NULL
			|| strftime(date, sizeof(date), "%a %b %d %H:%M", tm) == 0)
		return -1;

	return printf("%4zu %-20s %-32.32s %-30s\n", nth, date, letter->from,
		letter->subject == NULL ? "No Subject" : letter->subject);
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
		close(fd);
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
		close(fd);
	}
	return ret;
}

static void
letter_free(int type, struct letter *letter)
{
	free(letter->subject);
	free(letter->from);
	if (type == MAILBOX_MAILDIR)
		free(letter->ident.maildir_path);
}

static int
push_letter(int type, struct letter *letter, struct mailbox *mailbox)
{
	void *t;

	/* should be impossible */
	if (mailbox->nletters == LLONG_MAX)
		goto letter;

	t = reallocarray(mailbox->letters, mailbox->nletters + 1,
		sizeof(*mailbox->letters));
	if (t == NULL)
		goto letter;
	mailbox->letters = t;
	mailbox->letters[mailbox->nletters] = *letter;
	mailbox->nletters++;

	return 0;

	letter:
	free(letter->subject);
	free(letter->from);
	if (type == MAILBOX_MAILDIR)
		free(letter->ident.maildir_path);
	return -1;
}

static int
read_letter(FILE *fp, struct letter *letter)
{
	char *line = NULL;
	size_t n = 0;
	ssize_t len;

	letter->subject = NULL;
	letter->from = NULL;
	letter->date = -1;
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
		if (header_push(&header, letter) == -1)
			goto letter;
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

static char *
from_extract(char *from)
{
	char *m;
	if ((m = strchr(from, '<')) != NULL) {
		char *rv;
		size_t len;

		len = strlen(m);
		if (m[len - 1] != '>')
			return NULL;

		if ((rv = strndup(&m[1], len - 2)) == NULL)
			return NULL;
		free(from);
		return rv;
	}

	return from;
}

static int
header_push(struct header *header, struct letter *letter)
{
	if (!strcasecmp(header->key, "Subject")) {
		if (letter->subject != NULL)
			goto header;
		letter->subject = header->val;
		free(header->key);
		return 0;
	}
	else if (!strcasecmp(header->key, "From")) {
		if (letter->from != NULL)
			goto header;
		/* takes ownership of header->val on success */
		if ((letter->from = from_extract(header->val)) == NULL)
			goto header;
		free(header->key);
		return 0;
	}
	else if (!strcasecmp(header->key, "Date")) {
		struct tm tm;
		char *tz, *b;
		time_t off;
		const char *fmt;

		if (letter->date != -1)
			goto header;

		/* dont yet support ignoring comments except in this common case */
		if ((b = strrchr(header->val, '(')) != NULL)
			*b = '\0';
		if ((tz = strrchr(header->val, ' ')) == NULL)
			goto header;
		*tz++ = '\0';
		if ((off = tz_tosec(tz)) == TZ_INVALIDSEC)
			goto header;
		memset(&tm, 0, sizeof(tm));
		if (strchr(header->val, ',') != NULL)
			fmt = "%a, %d %b %Y %H:%M:%S";
		else
			fmt = "%d %b %Y %H:%M:%S";
		if (strptime(header->val, fmt, &tm) == NULL)
			goto header;
		if ((letter->date = mktime(&tm)) == -1)
			goto header;
		if (letter->date < off)
			goto header;
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

	header:
	free(header->key);
	free(header->val);
	return -1;
}

static int
letter_cmp(const void *one, const void *two)
{
	const struct letter *n1 = one, *n2 = two;
	time_t rv;

	rv = n1->date - n2->date;
	if (rv < 0)
		return -1;
	else if (rv > 0)
		return 1;
	else
		return 0;
}

static int
header_read(FILE *fp, char **lp, size_t *np, struct header *out)
{
	size_t vlen;

	if ((out->val = strchr(*lp, ':')) == NULL)
		return -1;
	*out->val++ = '\0';
	/* strip trailing ws */
	out->val += strspn(out->val, " \t");

	out->key = *lp;
	/* strip trailing ws */
	out->key[strcspn(out->key, " \t")] = '\0';

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
	if ((out->val = strndup(out->val, vlen)) == NULL)
		goto key;

	for (;;) {
		char *line;
		int c;
		void *t;
		ssize_t len, ws;

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

		ws = strspn(*lp, " \t");
		len -= ws;
		line = (*lp) + ws;

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
	int n, curfd, newfd;

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

	closedir(new);
	return cur;

	new:
	closedir(new);
	cur:
	closedir(cur);
	return NULL;
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
		if (fseek(fp, letter->ident.mbox_offset, SEEK_SET) == -1)
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
		RB_REMOVE(headers, &headers, v);
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
			int n;

			if ((c = fgetc(fp)) == EOF)
				break;
			if (c != '\n') {
				if (fseek(fp, -1, SEEK_CUR) == -1)
					goto headers;
				continue;
			}

			if ((n = fread(from, 1, 4, fp)) == EOF)
				break;
			if (n == 4 && memcmp(from, "From", 4) == 0)
				break;
			/* allow these characters to be dealth with as normal */
			if (fseek(fp, -n, SEEK_CUR) == -1)
				goto headers;
		}
	}

	if (mailbox_letter_mark_read(mailbox, letter) == -1)
		goto headers;

	rv = 0;
	headers:
	RB_FOREACH_SAFE(h, headers, &headers, h2) {
		RB_REMOVE(headers, &headers, h);
		free(h->key);
		free(h->val);
		free(h);
	}

	free(line);
	if (mailbox->type == MAILBOX_MAILDIR)
		fclose(fp);
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
	char s[3];
	int t;

	if ((t = fgetc(fp)) == EOF)
		return '=';
	s[0] = (char) t;
	if (s[0] == '\n')
		return '\n';
	if (!isxdigit(s[0])) {
		if (fseek(fp, -1, SEEK_CUR) == -1)
			return EOF;
		return '=';
	}
	if ((t = fgetc(fp)) == EOF)
		return '=';
	s[1] = (char) t;
	if (!isxdigit(s[1])) {
		if (fseek(fp, -2, SEEK_CUR) == -1)
			return EOF;
		return '=';
	}
	s[2] = '\0';

	return strtol(s, NULL, 16);
}

