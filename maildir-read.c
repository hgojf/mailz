#include <sys/stat.h>
#include <sys/time.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "address.h"
#include "getline.h"
#include "header.h"
#include "maildir-cache-read.h"

struct letter {
	time_t date;
	struct from_safe from;
	char *subject;
};

static struct maildir_cache_entry *cache_find(struct maildir_cache *, const char *);
static int cache_path_cmp(const void *, const void *);
static FILE *fopenat(int, const char *);
static int letter_read(FILE *, struct getline *, struct letter *);
static int letter_write(FILE *, const char *, struct letter *);
static int maildir_letter_seen(const char *);
static DIR *opendirat(int, const char *);
static int read_cache(int, struct maildir_cache *);
static time_t rfc5322_dateparse(char *);

/* argument is the cur directory of the maildir */
int
main(int argc, char *argv[])
{
	struct maildir_cache cache;
	DIR *dp;
	struct getline gl;
	ssize_t nw;
	int ch, dfd, root, rv, view_all;
	uint8_t need_recache;

	view_all = 0;
	while ((ch = getopt(argc, argv, "a")) != -1) {
		switch (ch) {
		case 'a':
			view_all = 1;
			break;
		default:
			errx(1, "invalid usage");
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		errx(1, "invalid usage");

	if (unveil(argv[0], "r") == -1)
		err(1, "%s", argv[0]);

	if (pledge("stdio rpath flock", NULL) == -1)
		err(1, "pledge");

	if ((root = open(argv[0], O_RDONLY | O_DIRECTORY)) == -1)
		err(1, "%s", argv[0]);

	rv = 1;
	if ((dp = opendirat(root, "cur")) == NULL) {
		warn("%s/cur", argv[0]);
		goto root;
	}
	dfd = dirfd(dp);

	if (read_cache(root, &cache) == -1) {
		warnx("failed to read cache");
		goto root;
	}

	if (pledge("stdio rpath", NULL) == -1) {
		warn("pledge");
		goto root;
	}

	if (cache.nletters != 0) {
		struct stat sb;

		if (fstat(dfd, &sb) == -1) {
			warn("fstat");
			goto cache;
		}

		if ((!view_all || cache.view_seen) && timespeccmp(&cache.mtime, &sb.st_mtim, >=)) {
			need_recache = 0;
			if (fwrite(&need_recache, sizeof(need_recache), 1, stdout) != 1) {
				warn("fwrite");
				goto cache;
			}

			for (size_t i = 0; i < cache.nletters; i++) {
				struct letter letter;

				if (!view_all && maildir_letter_seen(cache.letters[i].path))
					continue;

				letter.date = cache.letters[i].date;
				letter.from = cache.letters[i].from;
				letter.subject = cache.letters[i].subject;

				if (letter_write(stdout, cache.letters[i].path, &letter) == -1) {
					warn("write");
					goto cache;
				}
			}

			rv = 0;
			goto cache;
		}
		/*
		 * even if the cache is out of date, most of the entries can still
		 * be used. (assuming that the contents of a letter with a given
		 * name will never change).
		 * this assumes that the cache was sorted within the file.
		 */
	}
	/* otherwise bsearch will just return NULL at each check */
	need_recache = 1;

	if (fwrite(&need_recache, sizeof(need_recache), 1, stdout) != 1) {
		warn("write");
		goto cache;
	}

	memset(&gl, 0, sizeof(gl));
	for (;;) {
		struct letter letter;
		struct maildir_cache_entry *cached;
		struct dirent *de;
		FILE *fp;
		int r;

		errno = 0;
		if ((de = readdir(dp)) == NULL) {
			if (errno != 0) {
				warn("readdir");
				goto gl;
			}
			break;
		}

		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;
		if (!view_all && maildir_letter_seen(de->d_name))
			continue;

		cached = bsearch(de->d_name, cache.letters, cache.nletters, 
			sizeof(*cache.letters), cache_path_cmp);
		if (cached != NULL) {
			struct letter letter;

			letter.date = cached->date;
			letter.from = cached->from;
			letter.subject = cached->subject;

			if (letter_write(stdout, cached->path, &letter) == -1) {
				warn("write");
				goto gl;
			}
			continue;
		}

		if ((fp = fopenat(dfd, de->d_name)) == NULL) {
			warn("%s/%s", argv[0], de->d_name);
			goto gl;
		}
		if (letter_read(fp, &gl, &letter) == -1) {
			warnx("failed to read letter");
			(void) fclose(fp);
			goto gl;
		}
		if (fclose(fp) == EOF) {
			warn("fclose");
			free(letter.from.str);
			free(letter.subject);
			goto gl;
		}

		if (letter_write(stdout, de->d_name, &letter) == -1) {
			warn("write");
			free(letter.from.str);
			free(letter.subject);
			goto gl;
		}
		free(letter.from.str);
		free(letter.subject);
	}

	rv = 0;
	gl:
	free(gl.line);
	cache:
	for (size_t i = 0; i < cache.nletters; i++) {
		free(cache.letters[i].from.str);
		free(cache.letters[i].path);
		free(cache.letters[i].subject);
	}
	free(cache.letters);
	dir:
	if (closedir(dp) == -1) {
		warn("closedir");
		rv = 1;
	}
	root:
	if (close(root) == -1) {
		warn("close");
		rv = 1;
	}
	return rv;
}

static int
letter_write(FILE *out, const char *path, struct letter *letter)
{
	if (fwrite(&letter->date, sizeof(letter->date), 1, out) != 1)
		return -1;
	if (fwrite(path, strlen(path) + 1, 1, out) != 1)
		return -1;
	if (fwrite(letter->from.str, strlen(letter->from.str) + 1, 1, out) != 1)
		return -1;
	if (letter->subject != NULL) {
		if (fwrite(letter->subject, strlen(letter->subject) + 1, 1, out) != 1)
			return -1;
	}
	else {
		if (fputc('\0', out) == EOF)
			return -1;
	}
	return 0;
}

static FILE *
fopenat(int at, const char *name)
{
	int fd;
	FILE *fp;

	if ((fd = openat(at, name, O_RDONLY)) == -1)
		return NULL;
	if ((fp = fdopen(fd, "r")) == NULL) {
		(void) close(fd);
	}
	return fp;

}

static int
letter_read(FILE *fp, struct getline *gl, struct letter *out)
{
	struct from_safe from;
	char *subject;
	time_t date;

	from.str = NULL;
	subject = NULL;
	date = -1;
	for (;;) {
		struct header header;

		switch (header_read(fp, gl, &header, 1)) {
		case HEADER_ERR:
			goto fail;
		case HEADER_EOF:
			goto done;
		default:
			break;
		}

		if (!strcasecmp(header.key, "date")) {
			if (date != -1) {
				free(header.key);
				free(header.val);
				goto fail;
			}

			date = rfc5322_dateparse(header.val);
			free(header.key);
			free(header.val);
			if (date == -1)
				goto fail;
		}
		else if (!strcasecmp(header.key, "from")) {
			if (from.str != NULL) {
				free(header.key);
				free(header.val);
				goto fail;
			}
			if (from_safe_new(header.val, &from) == -1) {
				free(header.key);
				free(header.val);
				goto fail;
			}
			free(header.key);
		}
		else if (!strcasecmp(header.key, "subject")) {
			if (subject != NULL) {
				free(header.key);
				free(header.val);
				goto fail;
			}
			subject = header.val;
			free(header.key);
		}
		else {
			/* ignore */
			free(header.key);
			free(header.val);
		}
	}

	if (date == -1 || from.str == NULL)
		goto fail;

	done:
	out->date = date;
	out->from = from;
	out->subject = subject;
	return 0;

	fail:
	free(from.str);
	free(subject);
	return -1;
}

static time_t
rfc5322_dateparse(char *s)
{
	const char *fmt;
	char *b;
	struct tm tm;
	time_t rv;
	long off;

	if ((b = strrchr(s, '(')) != NULL)
		*b = '\0';

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

static DIR *
opendirat(int at, const char *path)
{
	int fd;
	DIR *dp;

	if ((fd = openat(at, path, O_RDONLY | O_DIRECTORY)) == -1)
		return NULL;
	if ((dp = fdopendir(fd)) == NULL)
		(void) close(fd);
	return dp;
}

static int
read_cache(int root, struct maildir_cache *out)
{
	FILE *fp;
	int fd, rv;

	if ((fd = openat(root, ".mailzcache", O_RDONLY)) == -1) {
		if (errno != ENOENT)
			return -1;
		out->nletters = 0;
		out->letters = NULL;
		return 0;
	}

	if ((fp = fdopen(fd, "r")) == NULL) {
		(void) close(fd);
		return -1;
	}

	rv = maildir_cache_read(fp, out);
	fclose(fp);
	if (rv == -1) {
		out->nletters = 0;
		out->letters = NULL;
		return 0;
	}
	/* otherwise maildir_cache_read has set them */
	return 0;
}

static int
cache_path_cmp(const void *one, const void *two)
{
	const char *n1 = one;
	const struct maildir_cache_entry *n2 = two;

	return strcmp(one, n2->path);
}

static int
maildir_letter_seen(const char *path)
{
	const char *flags;

	return (flags = strstr(path, ":2,")) != NULL && strchr(flags, 'S') != NULL;
}
