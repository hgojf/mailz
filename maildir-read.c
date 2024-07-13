#include <dirent.h>
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
#include "maildir-read.h"

struct letter {
	time_t date;
	struct from_safe from;
	char *subject;
};

static FILE *fopenat(int, const char *);
static int letter_read(FILE *, struct getline *, struct letter *);
static int letter_write(FILE *, const char *, struct letter *);
static time_t rfc5322_dateparse(char *);

/* argument is the cur directory of the maildir */
int
main(int argc, char *argv[])
{
	DIR *dp;
	struct getline gl;
	ssize_t nw;
	int dfd, rv, save_errno;

	if (argc != 2) {
		save_errno = 0;
		rv = MAILDIR_READ_USAGE;
		goto fail;
	}

	if (unveil(argv[1], "r") == -1) {
		save_errno = errno;
		rv = MAILDIR_READ_UNVEIL;
		goto fail;
	}

	if (pledge("stdio rpath", NULL) == -1) {
		save_errno = errno;
		rv = MAILDIR_READ_PLEDGE;
		goto fail;
	}

	if ((dp = opendir(argv[1])) == NULL) {
		save_errno = errno;
		rv = MAILDIR_READ_OPENDIR;
		goto fail;
	}
	dfd = dirfd(dp);


	memset(&gl, 0, sizeof(gl));
	for (;;) {
		struct letter letter;
		struct dirent *de;
		FILE *fp;
		int r;

		errno = 0;
		if ((de = readdir(dp)) == NULL) {
			if (errno != 0) {
				save_errno = errno;
				rv = MAILDIR_READ_READDIR;
				goto gl;
			}
			break;
		}

		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;

		if ((fp = fopenat(dfd, de->d_name)) == NULL) {
			save_errno = errno;
			rv = MAILDIR_READ_FOPEN;
			goto gl;
		}
		if (letter_read(fp, &gl, &letter) == -1) {
			save_errno = errno;
			rv = MAILDIR_READ_LETTER;
			(void) fclose(fp);
			goto gl;
		}
		if (fclose(fp) == EOF) {
			save_errno = errno;
			rv = MAILDIR_READ_FCLOSE;
			free(letter.from.str);
			free(letter.subject);
			goto gl;
		}

		if (letter_write(stdout, de->d_name, &letter) == -1) {
			save_errno = errno;
			rv = MAILDIR_READ_WRITE;
			free(letter.from.str);
			free(letter.subject);
			goto gl;
		}
		free(letter.from.str);
		free(letter.subject);
	}

	save_errno = 0;
	rv = MAILDIR_READ_OK;
	gl:
	free(gl.line);
	dir:
	if (closedir(dp) == -1 && rv == MAILDIR_READ_OK) {
		save_errno = errno;
		rv = MAILDIR_READ_CLOSEDIR;
	}
	fail:
	nw = write(STDERR_FILENO, &save_errno, sizeof(save_errno));
	if (nw == -1)
		rv = MAILDIR_READ_WRITE;
	else if (nw != sizeof(save_errno))
		rv = MAILDIR_READ_SWRITE;
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
