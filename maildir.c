#include <sys/tree.h>

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mail.h"
#include "maildir.h"

struct header {
	char *key;
	char *val;
	RB_ENTRY(header) entries;
};

RB_HEAD(headers, header);
RB_PROTOTYPE(headers, header, entry, header_cmp);

static DIR *opendirat(int, const char *);
static FILE *fopenat(int, const char *);

static int header_read(FILE *, char **, size_t *, struct header *);
static int header_push(struct header *, struct maildir_letter *);
static int read_letter(const char *, FILE *, struct maildir_letter *);
static int push_letter(struct maildir_letter *, struct maildir *);
static int letter_cmp(const void *, const void *);

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

int
maildir_setup(int dfd, struct maildir *maildir)
{
	DIR *cur, *new;
	struct dirent *de;
	char name[NAME_MAX];
	int n, curfd, newfd;

	if ((cur = opendirat(dfd, "cur")) == NULL)
		return -1;
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

	maildir->cur = cur;
	maildir->nletters = 0;
	maildir->letters = NULL;

	closedir(new);
	return 0;

	new:
	closedir(new);
	cur:
	closedir(cur);
	return -1;
}

int
maildir_read(struct maildir *maildir, const struct options *options)
{
	struct dirent *de;
	int dfd = dirfd(maildir->cur);

	while ((de = readdir(maildir->cur)) != NULL) {
		char *flags, flag;
		int n, rv;
		struct maildir_letter letter;
		FILE *fp;

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;

		if ((flags = strchr(de->d_name, ':')) == NULL)
			goto letters;
		flags++;
		n = sscanf(flags, "2,%c", &flag);
		if (n == EOF) { /* no flags */
		}
		else if (n == 1) {
			if (flag == 'S' && !options->view_seen)
				continue;
		}

		if ((fp = fopenat(dfd, de->d_name)) == NULL)
			goto letters;

		rv = read_letter(de->d_name, fp, &letter);
		fclose(fp);
		if (rv == -1)
			goto letters;
		if (push_letter(&letter, maildir) == -1)
			goto letters;
	}

	qsort(maildir->letters, maildir->nletters, sizeof(*maildir->letters),
		letter_cmp);
	return 0;

	letters:
	for (size_t i = 0; i < maildir->nletters; i++) {
		free(maildir->letters[i].path);
		free(maildir->letters[i].subject);
		free(maildir->letters[i].from);
	}
	free(maildir->letters);
	return -1;
}

static int
push_letter(struct maildir_letter *letter, struct maildir *maildir)
{
	void *t;

	t = reallocarray(maildir->letters, maildir->nletters + 1,
		sizeof(*maildir->letters));
	if (t == NULL)
		goto letter;
	maildir->letters = t;
	maildir->letters[maildir->nletters] = *letter;
	maildir->nletters++;

	return 0;

	letter:
	free(letter->subject);
	free(letter->from);
	free(letter->path);
	return -1;
}

static int
read_letter(const char *name, FILE *fp, struct maildir_letter *letter)
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

	if (letter->subject == NULL || letter->from == NULL || letter->date == -1)
		goto letter;

	if ((letter->path = strdup(name)) == NULL)
		goto letter;

	free(line);
	return 0;

	letter:
	free(letter->subject);
	free(letter->from);

	free(line);
	return -1;
}

static int
header_push(struct header *header, struct maildir_letter *letter)
{
	if (!strcmp(header->key, "Subject")) {
		if (letter->subject != NULL)
			goto header;
		letter->subject = header->val;
		free(header->key);
		return 0;
	}
	else if (!strcmp(header->key, "From")) {
		if (letter->from != NULL)
			goto header;
		letter->from = header->val;
		free(header->key);
		return 0;
	}
	else if (!strcmp(header->key, "Date")) {
		struct tm tm;

		if (letter->date != -1)
			goto header;

		if (strptime(header->val, "%a, %d %b %Y %H:%M:%S %z", &tm) == NULL)
			goto header;
		if ((letter->date = mktime(&tm)) == -1)
			goto header;
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
	const struct maildir_letter *n1 = one, *n2 = two;
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
header_ignore(struct header *header, const struct options *options)
{
	if (options->nunignore != 0) {
		for (size_t i = 0; i < options->nunignore; i++) {
			if (!strcmp(header->key, options->unignore[i]))
				return 0;
		}
		return 1;
	}
	else {
		for (size_t i = 0; i < options->nignore; i++) {
			if (!strcmp(header->key, options->ignore[i]))
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
		(void) RB_INSERT(headers, headers, hp);
	}

	return 0;

	header:
	free(header->val);
	free(header->key);
	return -1;
}

int
maildir_letter_set_flag(struct maildir *maildir, struct maildir_letter *letter, char f)
{
	char name[NAME_MAX], *flags, *t;
	int n, dfd;

	/* remove existing flags, if they exist */
	if ((flags = strchr(letter->path, ':')) == NULL)
		return -1;
	*flags = '\0';

	/* hide flags for this call */
	n = snprintf(name, NAME_MAX, "%s:2,%c", letter->path, f);

	/* now unhide */
	*flags = ':';

	if (n < 0 || n >= NAME_MAX)
		return -1;


	dfd = dirfd(maildir->cur);

	if (renameat(dfd, letter->path, dfd, name) == -1)
		return -1;

	/* need to dup in case flags changed, could be more selective */
	t = strdup(name);
	if (t == NULL)
		return -1;
	free(letter->path);
	letter->path = t;
	return 0;
}

#define LOCALE_NONE 0
#define LOCALE_UTF8 1

#define SOFT_BREAK 256
static int
equal_escape(FILE *fp, int locale)
{
	char s[3], rv;

	if ((s[0] = fgetc(fp)) == EOF)
		return '=';
	if (s[0] == '\n')
		return SOFT_BREAK;
	if (!isxdigit(s[0])) {
		if (ungetc(s[0], fp) == EOF)
			return EOF;
		return '=';
	}
	if ((s[1] = fgetc(fp)) == EOF)
		return '=';
	if (!isxdigit(s[1])) {
		if (ungetc(s[0], fp) == EOF)
				return EOF;
		if (ungetc(s[1], fp) == EOF)
			return EOF;
		return '=';
	}
	s[2] = '\0';

	rv = strtol(s, NULL, 16);

	switch (locale) {
	case LOCALE_NONE:
	case LOCALE_UTF8:
	/* utf-8 should do mbrtowc */
		if (!isprint(rv))
			return EOF;
		return rv;
	default:
		/* NOTREACHED */
		abort();
	}
}

static int
locale_find(struct headers *headers)
{
	struct header w, *f;
	char *cs, *val;

	w.key = "Content-Type";
	if ((f = RB_FIND(headers, headers, &w)) == NULL)
		return 0;

	val = f->val;

	while ((cs = strsep(&val, ";")) != NULL) {
		char *v, *q;

		cs += strspn(cs, " \t");
		if ((v = strchr(cs, '=')) == NULL)
			continue;
		*v++ = '\0';
		if (strcmp(cs, "charset") != 0)
			continue;
		if (v[0] == '\"')
			v++;
		if ((q = strchr(v, '\"')) != NULL)
			*q = '\0';
		if (!strcasecmp("utf-8", v))
			return LOCALE_UTF8;
	}

	return LOCALE_NONE;
}

int
maildir_letter_print_read(struct maildir *maildir,
struct maildir_letter *letter, const struct options *options,
FILE *out)
{
	FILE *fp;
	char *line = NULL;
	size_t n = 0;
	ssize_t len;
	struct headers headers;
	struct header *h, *h2;
	int c, locale;
	int rv = -1;

	if ((fp = fopenat(dirfd(maildir->cur), letter->path)) == NULL)
		return -1;

	RB_INIT(&headers);

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
		struct header *h, f;

		f.key = options->reorder[i];
		if ((h = RB_FIND(headers, &headers, &f)) == NULL)
			continue;
		if (!header_ignore(h, options) 
				&& fprintf(out, "%s: %s\n", h->key, h->val) < 0)
			goto headers;
		RB_REMOVE(headers, &headers, h);
		free(h->key);
		free(h->val);
		free(h);
	}

	RB_FOREACH(h, headers, &headers) {
		if (!header_ignore(h, options) && fprintf(out, "%s: %s\n", h->key, h->val) < 0)
			goto headers;
	}

	locale = locale_find(&headers);

	while ((c = fgetc(fp)) != EOF) {
		if (c == '=') {
			switch (c = equal_escape(fp, locale)) {
			case EOF:
				goto headers;
			case SOFT_BREAK:
				continue;
			default:
				break;
			}
		}
		if (fputc(c, out) == EOF)
			goto headers;
	}

	if (maildir_letter_set_flag(maildir, letter, 'S') == -1)
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
	fclose(fp);
	return rv;
}

void
maildir_free(struct maildir *maildir)
{
	closedir(maildir->cur);
	for (size_t i = 0; i < maildir->nletters; i++) {
		free(maildir->letters[i].path);
		free(maildir->letters[i].subject);
		free(maildir->letters[i].from);
	}
	free(maildir->letters);
}

int
maildir_letter_print(size_t nth, struct maildir_letter *letter)
{
	char date[30];
	struct tm *tm;

	if ((tm = localtime(&letter->date)) == NULL) {
		strlcpy(date, "Unknown date", sizeof(date));
	}
	else {
		if (strftime(date, sizeof(date), "%a %b %d %H:%M", tm) == 0)
			strlcpy(date, "Unknown date", sizeof(date));
	}

	return printf("%-5zu%s %s %s\n", nth, date, letter->from, 
		letter->subject);
}

int
maildir_print(struct maildir *mail, size_t b, size_t e)
{
	for (; b < e; b++) {
		if (maildir_letter_print(b + 1, &mail->letters[b]) == -1)
			return -1;
	}
	return 0;
}

static int
header_cmp(struct header *one, struct header *two)
{
	return strcmp(one->key, two->key);
}

RB_GENERATE(headers, header, entries, header_cmp);

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
		if (out->key[i] < 33 || out->key[i] > 126)
			return -1;
	}

	for (vlen = 0; out->val[vlen] != '\0'; vlen++) {
		if (out->val[vlen] > 127)
			return -1;
	}

	if ((out->key = strdup(out->key)) == NULL)
		return -1;
	if ((out->val = strndup(out->val, vlen)) == NULL)
		goto key;

	for (;;) {
		char c, *line;
		void *t;
		ssize_t len, ws;

		if ((c = fgetc(fp)) == EOF)
			goto val;
		if (!isspace(c) || c == '\n') {
			if (ungetc(c, fp) == EOF)
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
			if (line[i] > 127)
				goto val;
		}

		t = realloc(out->val, vlen + len + 1);
		if (t == NULL)
			goto val;
		out->val = t;
		memcpy(&out->val[vlen], line, len);
		out->val[vlen + len] = '\0';
	}

	return 0;

	val:
	free(out->val);
	key:
	free(out->key);
	return -1;
}
