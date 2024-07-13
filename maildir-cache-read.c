#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "getline.h"
#include "maildir-cache.h"
#include "maildir-cache-read.h"

struct cache_entry {
	char *path;
	char *subject;
	char *from;
	time_t date;
};

#define CACHE_ENTRY_STRDUP -3
#define CACHE_ENTRY_FERR -2
#define CACHE_ENTRY_EOF -1

static int cache_entry_read(FILE *, struct getline *, struct cache_entry *);

int
main(int argc, char *argv[])
{
	struct stat sb;
	struct getline gl;
	FILE *fp;
	ssize_t nw;
	uint32_t magic;
	int fd, rv, save_errno;
	uint8_t view_seen;

	if (argc != 2) {
		save_errno = errno;
		rv = MAILDIR_CACHE_READ_USAGE;
		goto fail;
	}
	if ((fd = open(argv[1], O_RDONLY)) == -1) {
		save_errno = errno;
		rv = MAILDIR_CACHE_READ_OPEN;
		goto fail;
	}
	if (fstat(fd, &sb) == -1) {
		save_errno = errno;
		rv = MAILDIR_CACHE_READ_STAT;
		(void) close(fd);
		goto fail;
	}
	if ((fp = fdopen(fd, "r")) == NULL) {
		save_errno = errno;
		rv = MAILDIR_CACHE_READ_FOPEN;
		(void) close(fd);
		goto fail;
	}

	if (pledge("stdio", NULL) == -1) {
		save_errno = errno;
		rv = MAILDIR_CACHE_READ_PLEDGE;
		goto fp;
	}

	if (fwrite(&sb.st_mtime, sizeof(sb.st_mtime), 1, stdout) != 1) {
		save_errno = errno;
		rv = MAILDIR_CACHE_READ_FWRITE;
		goto fp;
	}

	if (fread(&magic, sizeof(magic), 1, fp) != 1) {
		save_errno = errno;
		rv = MAILDIR_CACHE_READ_FREAD;
		goto fp;
	}

	if ((magic & MAILDIR_CACHE_MAGIC_MASK) != MAILDIR_CACHE_MAGIC) {
		save_errno = 0;
		rv = MAILDIR_CACHE_READ_MAGIC;
		goto fp;
	}
	if ((magic & MAILDIR_CACHE_VERSION_MASK) != MAILDIR_CACHE_VERSION) {
		save_errno = 0;
		rv = MAILDIR_CACHE_READ_VERSION;
		goto fp;
	}

	if (fread(&view_seen, sizeof(view_seen), 1, fp) != 1) {
		save_errno = errno;
		rv = MAILDIR_CACHE_READ_FREAD;
		goto fp;
	}

	memset(&gl, 0, sizeof(gl));
	for (;;) {
		struct cache_entry entry;

		switch (cache_entry_read(fp, &gl, &entry)) {
		case CACHE_ENTRY_FERR:
			save_errno = errno;
			rv = MAILDIR_CACHE_READ_ENTRY;
			goto fp;
		case CACHE_ENTRY_STRDUP:
			save_errno = errno;
			rv = MAILDIR_CACHE_READ_MALLOC;
			goto fp;
		case CACHE_ENTRY_EOF:
			goto done;
		default:
			break;
		}
	}
	done:

	save_errno = 0;
	rv = MAILDIR_CACHE_READ_OK;
	fp:
	if (fclose(fp) == EOF && rv == MAILDIR_CACHE_READ_OK) {
		save_errno = errno;
		rv = MAILDIR_CACHE_READ_CLOSE;
	}
	fail:
	nw = write(STDERR_FILENO, &save_errno, sizeof(save_errno));
	if (nw == -1)
		rv = MAILDIR_CACHE_READ_WRITE;
	else if (nw != sizeof(save_errno))
		rv = MAILDIR_CACHE_READ_SWRITE;
	return rv;
}

static int
cache_entry_read(FILE *fp, struct getline *gl, struct cache_entry *out)
{
	size_t n;
	ssize_t len;
	char *from, *path, *subject;
	uint64_t date;
	int rv;

	n = fread(&date, 1, sizeof(date), fp);
	if (n == 0) {
		if (ferror(fp))
			return CACHE_ENTRY_FERR;
		return CACHE_ENTRY_EOF;
	}

	if (getdelim(&gl->line, &gl->n, '\0', fp) == -1)
		return CACHE_ENTRY_FERR;
	if ((path = strdup(gl->line)) == NULL)
		return CACHE_ENTRY_STRDUP;

	if ((len = getdelim(&gl->line, &gl->n, '\0', fp)) == -1) {
		rv = CACHE_ENTRY_FERR;
		goto path;
	}
	if (len == 0)
		subject = NULL;
	else {
		if ((subject = strdup(gl->line)) == NULL) {
			rv = CACHE_ENTRY_STRDUP;
			goto path;
		}
	}

	if (getdelim(&gl->line, &gl->n, '\0', fp) == -1) {
		rv = CACHE_ENTRY_FERR;
		goto subject;
	}
	if ((from = strdup(gl->line)) == NULL) {
		rv = CACHE_ENTRY_STRDUP;
		goto subject;
	}

	out->date = date;
	out->from = from;
	out->path = path;
	out->subject = subject;
	return 0;

	subject:
	free(subject);
	path:
	free(path);
	return rv;
}
