#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "address.h"
#include "getline.h"
#include "letter.h"
#include "maildir-cache.h"
#include "maildir-cache-read.h"

#define CACHE_ENTRY_ERR -2
#define CACHE_ENTRY_EOF -1

static int cache_entry_read(FILE *, struct getline *, struct maildir_cache_entry *);

int
maildir_cache_read(FILE *fp, struct maildir_cache *out)
{
	struct stat sb;
	struct getline gl;
	struct maildir_cache_entry *letters;
	size_t nletters;
	uint32_t magic;
	int fd, rv;
	uint8_t view_seen;

	if ((fd = fileno(fp)) == -1)
		return -1;

	if (fstat(fd, &sb) == -1)
		return -1;

	if (flock(fd, LOCK_SH) == -1)
		return -1;

	rv = -1;

	if (fread(&magic, sizeof(magic), 1, fp) != 1)
		goto lock;

	if ((magic & MAILDIR_CACHE_MAGIC_MASK) != MAILDIR_CACHE_MAGIC)
		goto lock;
	if ((magic & MAILDIR_CACHE_VERSION_MASK) != MAILDIR_CACHE_VERSION)
		goto lock;

	if (fread(&view_seen, sizeof(view_seen), 1, fp) != 1)
		goto lock;

	letters = NULL;
	nletters = 0;
	memset(&gl, 0, sizeof(gl));
	for (;;) {
		struct maildir_cache_entry entry, *t;

		switch (cache_entry_read(fp, &gl, &entry)) {
		case CACHE_ENTRY_ERR:
			goto letters;
		case CACHE_ENTRY_EOF:
			goto done;
		default:
			break;
		}

		t = reallocarray(letters, nletters + 1, sizeof(*letters));
		if (t == NULL) {
			free(entry.from.str);
			free(entry.path);
			free(entry.subject);
			goto letters;
		}

		letters = t;
		letters[nletters++] = entry;
	}
	done:

	rv = 0;
	letters:
	if (rv == -1) {
		for (size_t i = 0; i < nletters; i++) {
			free(letters[i].from.str);
			free(letters[i].path);
			free(letters[i].subject);
		}
		free(letters);
	}
	else {
		out->letters = letters;
		out->nletters = nletters;
		out->mtime = sb.st_mtim;
		out->view_seen = view_seen;
	}
	free(gl.line);
	lock:
	if (flock(fd, LOCK_UN) == -1)
		rv = -1;
	return rv;
}

static int
cache_entry_read(FILE *fp, struct getline *gl, struct maildir_cache_entry *out)
{
	size_t n;
	ssize_t len;
	struct from_safe from;
	char *f, *path, *subject;
	uint64_t date;

	n = fread(&date, 1, sizeof(date), fp);
	if (n == 0) {
		if (ferror(fp))
			return CACHE_ENTRY_ERR;
		return CACHE_ENTRY_EOF;
	}
	else if (n != sizeof(date))
		return CACHE_ENTRY_ERR;

	if (getdelim(&gl->line, &gl->n, '\0', fp) == -1)
		return CACHE_ENTRY_ERR;
	if ((path = strdup(gl->line)) == NULL)
		return CACHE_ENTRY_ERR;

	if ((len = getdelim(&gl->line, &gl->n, '\0', fp)) == -1)
		goto path;
	if (len == 0)
		subject = NULL;
	else {
		if ((subject = strdup(gl->line)) == NULL)
			goto path;
	}

	if (getdelim(&gl->line, &gl->n, '\0', fp) == -1)
		goto subject;
	if ((f = strdup(gl->line)) == NULL)
		goto subject;
	if (from_safe_new(f, &from) == -1) {
		free(f);
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
	return CACHE_ENTRY_ERR;
}
