#include <sys/tree.h>

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
#include "mail-util.h"

static DIR *opendirat(int, const char *);
static FILE *fopenat(int, const char *);

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
	return n1->date - n2->date;
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
header_push2(struct header *header, struct headers *headers,
const struct options *options)
{
	struct header *fh, *hp;

	if (header_ignore(header, options)) {
		free(header->key);
		free(header->val);
		return 0;
	}
	else if ((fh = RB_FIND(headers, headers, header)) != NULL) {
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
	size_t len;

	/* remove existing flags, if they exist */
	if ((flags = strchr(letter->path, ':')) == NULL)
		return -1;
	len = flags - letter->path;
	if (len > INT_MAX)
		return -1;

	n = snprintf(name, NAME_MAX, "%.*s:2,%c", (int) len, letter->path, f);
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
	char buf[4096];
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
		if (header_push2(&header, &headers, options) == -1)
			goto headers;
	}

	RB_FOREACH(h, headers, &headers) {
		if (fprintf(out, "%s: %s\n", h->key, h->val) < 0)
			goto headers;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (fputs(buf, out) == -1)
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

static int
letter_print(size_t nth, struct maildir_letter *letter)
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

	return printf("%zu    %s %s %s\n", nth, date, letter->from, 
		letter->subject);
}

int
maildir_print(struct maildir *mail, size_t b, size_t e)
{
	for (; b < e; b++) {
		if (letter_print(b + 1, &mail->letters[b]) == -1)
			return -1;
	}
	return 0;
}
