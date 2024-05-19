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
#include "mail-util.h"

static DIR *opendirat(int, const char *);
static FILE *fopenat(int, const char *);

static int read_header(FILE *, char **, size_t *, struct header *);
static int read_letter(FILE *, struct letter *);

static int letter_cmp(const void *, const void *);

DIR *
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
		n =	snprintf(name, NAME_MAX, "%s:2,S", de->d_name);
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

static DIR *
opendirat(int at, const char *path)
{
	int fd;
	DIR *ret;

	if ((fd = openat(at, path, O_DIRECTORY | O_RDONLY)) == -1)
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

	if ((fd = openat(at, path, O_RDONLY)) == -1)
		return NULL;
	if ((ret = fdopen(fd, "r")) == NULL) {
		close(fd);
	}
	return ret;
}

static int
read_header(FILE *fp, char **lp, size_t *np, struct header *out)
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

static int
read_letter(FILE *fp, struct letter *letter)
{
	char *line = NULL;
	size_t n = 0;
	ssize_t len;
	struct header *n1, *n2;

	letter->sent = -1;
	RB_INIT(&letter->headers);
	for (;;) {
		struct header header, *hp, *fh;

		if ((len = getline(&line, &n, fp)) == -1)
			goto headers;
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		if (*line == '\0')
			break;
		if (read_header(fp, &line, &n, &header) == -1)
			goto headers;

		if (!strcmp(header.key, "Date")) {
			struct tm tm;

			if (strptime(header.val, "%a, %d %b %Y %H:%M:%S %z", &tm) == NULL)
				goto headers;
			if ((letter->sent = mktime(&tm)) == -1)
				goto headers;
		}
		else if ((fh = RB_FIND(headers, &letter->headers, &header)) != NULL) {
			void *t;
			size_t len, len1;

			len = strlen(fh->val);
			len1 = strlen(header.val);

			t = realloc(fh->val, len + len1 + 1);
			if (t == NULL)
				goto headers;
			fh->val = t;
			memcpy(&fh->val[len], header.val, len1);
			fh->val[len + len1] = '\0';

			free(header.key);
			free(header.val);
		}
		else {
			if ((hp = malloc(sizeof(*hp))) == NULL)
				goto headers;
			*hp = header;
			(void) RB_INSERT(headers, &letter->headers, hp);
		}
	}

	if (letter->sent == -1)
		goto headers;

	/* XXX: reading whole file into memory is bad */
	if (getdelim(&line, &n, EOF, fp) == -1)
		goto headers;
	letter->text = line;

	return 0;

	headers:
	RB_FOREACH_SAFE(n1, headers, &letter->headers, n2) {
		RB_REMOVE(headers, &letter->headers, n1);
		free(n1->key);
		free(n1->val);
		free(n1);
	}
	free(line);
	return -1;
}

int
maildir_read(DIR *dp, struct mail *mail, const struct options *options)
{
	struct dirent *de;
	int dfd;

	dfd = dirfd(dp);

	mail->nletters = 0;
	mail->letters = NULL;

	while ((de = readdir(dp)) != NULL) {
		struct letter letter;
		FILE *fp;
		void *t;
		int rv, n;
		char *flags, flag;

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;

		if ((flags = strrchr(de->d_name, ':')) == NULL)
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
		rv = read_letter(fp, &letter);
		fclose(fp);
		if (rv == -1)
			goto letters;

		t = reallocarray(mail->letters, mail->nletters + 1, 
			sizeof(*mail->letters));
		if (t == NULL)
			goto letters;

		mail->letters = t;
		mail->letters[mail->nletters] = letter;
		mail->nletters++;
	}

	qsort(mail->letters, mail->nletters, sizeof(*mail->letters), letter_cmp);

	closedir(dp);
	return 0;

	letters:
	closedir(dp);
	return -1;
}

static int
letter_cmp(const void *one, const void *two)
{
	const struct letter *n1 = one, *n2 = two;

	return n1->sent - n2->sent;
}
