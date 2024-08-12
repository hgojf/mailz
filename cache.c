#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "letter.h"
#include "cache.h"

#define MAILZCACHE_MAGIC_MASK	0xFFFFFF00
#define MAILZCACHE_MAGIC		0x48414D00	
#define MAILZCACHE_VERSION_MASK	0x000000FF
#define MAILZCACHE_VERSION		0x00000001

/*
 * Mailz cache format:
 * 0x48414D01 (32 bit integer). 
 *	This is MAILZCACHE_MAGIC | MAILZ_CACHE_VERSION
 * 0b0000000A (8 bit integer)
 * 	A - view_all bit, if set then letters which have been seen are included 
 *		in the cache.
 *	All other bits are ignored (for future expansion).
 *
 * The rest of the file contains an array of entries in the format:
 * date (64 bit signed integer)
 *		- This is a unix timestamp and must be within range for 
 *		- use with localtime(3).
 * from address (printable nul-terminated string)
 * from real name (optional printable nul-terminated string)
 * path (nul-terminated byte-string). This is relative to maildir/cur
 * message_id (optional printable nul terminated string)
 * subject (optional printable nul terminated string)
 *
 * For optional strings, an empty string signifies no data.
 * All integers are in little endian format
 */

struct getline {
	char *line;
	size_t sz;
};

static int fread_all(void *, size_t, FILE *);
static char *fread_bytestring(struct getline *, FILE *);
static int fread_letter(struct getline *, struct letter *, FILE *);
static int fread_optstring(struct getline *, char **, FILE *);
static char *fread_string(struct getline *, FILE *);

static int cache_write1(int, struct letter *, size_t, FILE *);
static int fwrite_all(const void *, size_t, FILE *);
static int fwrite_letter(const struct letter *, FILE *);
static int fwrite_string_opt(const char *, FILE *);
static int letter_cmp_path(const void *, const void *);

int
cache_read(FILE *fp, struct cache *out)
{
	struct getline gl;
	struct letter *letters;
	size_t nletter;
	uint32_t version;
	uint8_t flags;

	if (fread_all(&version, sizeof(version), fp) == -1)
		return -1;
	version = letoh32(version);

	if ((version & MAILZCACHE_MAGIC_MASK) != MAILZCACHE_MAGIC) {
		errno = EILSEQ;
		return -1;
	}
	if ((version & MAILZCACHE_VERSION_MASK) != MAILZCACHE_VERSION)
		return CACHE_VERSION_MISMATCH;

	if (fread_all(&flags, sizeof(flags), fp) == -1)
		return -1;

	memset(&gl, 0, sizeof(gl));
	letters = NULL;
	nletter = 0;
	for (;;) {
		struct letter letter, *t;

		switch (fread_letter(&gl, &letter, fp)) {
		case -2:
			goto done;
		case -1:
			goto letters;
		default:
			break;
		}
		t = reallocarray(letters, nletter + 1, sizeof(*letters));
		if (t == NULL) {
			letter_free(&letter);
			goto letters;
		}
		letters = t;
		letters[nletter++] = letter;
	}
	done:

	free(gl.line);

	out->view_all = flags & 0x1;
	out->entries = letters;
	out->nentry = nletter;
	return 0;

	letters:
	for (size_t i = 0; i < nletter; i++)
		letter_free(&letters[i]);
	free(letters);
	free(gl.line);
	return -1;
}

static int
fread_letter(struct getline *gl, struct letter *letter, FILE *fp)
{
	int64_t date;
	size_t n;

	n = fread(&date, 1, sizeof(date), fp);
	if (n == 0)
		return -2;
	if (n != sizeof(date))
		return -1;
	letter->date = letoh64(date);

	if ((letter->from.addr = fread_string(gl, fp)) == NULL)
		return -1;
	if (fread_optstring(gl, &letter->from.name, fp) == -1)
		goto addr;
	if ((letter->path = fread_bytestring(gl, fp)) == NULL)
		goto name;
	if (fread_optstring(gl, &letter->message_id, fp) == -1)
		goto path;
	if (fread_optstring(gl, &letter->subject, fp) == -1)
		goto message_id;

	return 0;

	message_id:
	free(letter->message_id);
	path:
	free(letter->path);
	name:
	free(letter->from.name);
	addr:
	free(letter->from.addr);
	return -1;
}

static int
fread_optstring(struct getline *gl, char **s, FILE *fp)
{
	ssize_t len;
	char *rv;

	if ((len = getdelim(&gl->line, &gl->sz, '\0', fp)) == -1)
		return -1;

	if (gl->line[len - 1] != '\0') {
		errno = EILSEQ;
		return -1;
	}

	if (gl->line[0] == '\0') {
		*s = NULL;
		return 0;
	}

	if ((rv = malloc(len)) == NULL)
		return -1;
	memcpy(rv, gl->line, len); /* includes NUL */

	*s = rv;
	return 0;
}

static char *
fread_bytestring(struct getline *gl, FILE *fp)
{
	ssize_t len;
	char *rv;

	if ((len = getdelim(&gl->line, &gl->sz, '\0', fp)) == -1)
		return NULL;

	if (gl->line[len - 1] != '\0') {
		errno = EILSEQ;
		return NULL;
	}

	if ((rv = malloc(len)) == NULL)
		return NULL;
	memcpy(rv, gl->line, len); /* includes NUL */

	return rv;
}

static char *
fread_string(struct getline *gl, FILE *fp)
{
	return fread_bytestring(gl, fp);
}

static int
fread_all(void *buf, size_t n, FILE *fp)
{
	if (fread(buf, n, 1, fp) != 1)
		return -1;
	return 0;
}

int
cache_write(int root, int view_all, struct letter *letters, size_t nl)
{
	FILE *fp;
	int fd, rv;

	if ((fd = openat(root, ".mailzcache", 
			O_WRONLY | O_TRUNC | O_CREAT, 0600)) == -1)
		return -1;
	if ((fp = fdopen(fd, "w")) == NULL) {
		close(fd);
		return -1;
	}

	rv = cache_write1(view_all, letters, nl, fp);

	fclose(fp);

	return rv;
}

static int
cache_write1(int view_all, struct letter *letters, size_t nl, FILE *out)
{
	uint32_t version;
	uint8_t flags;

	version = htole32(MAILZCACHE_MAGIC | MAILZCACHE_VERSION);
	flags = view_all != 0;

	if (fwrite_all(&version, sizeof(version), out) == -1)
		return -1;
	if (fwrite_all(&flags, sizeof(flags), out) == -1)
		return -1;

	qsort(letters, nl, sizeof(*letters), letter_cmp_path);

	for (size_t i = 0; i < nl; i++) {
		if (!view_all && letter_seen(letters[i].path))
			continue;

		if (fwrite_letter(&letters[i], out) == -1)
			return -1;
	}

	return 0;
}

static int
fwrite_letter(const struct letter *letter, FILE *out)
{
	int64_t date;

	date = htole64(letter->date);

	if (fwrite_all(&date, sizeof(date), out) == -1)
		return -1;
	if (fwrite_all(letter->from.addr, strlen(letter->from.addr) + 1, out) == -1)
		return -1;
	if (fwrite_string_opt(letter->from.name, out) == -1)
		return -1;
	if (fwrite_all(letter->path, strlen(letter->path) + 1, out) == -1)
		return -1;
	if (fwrite_string_opt(letter->message_id, out) == -1)
		return -1;
	if (fwrite_string_opt(letter->subject, out) == -1)
		return -1;
	return 0;
}

static int
fwrite_string_opt(const char *s, FILE *out)
{
	if (s == NULL)
		return fwrite_all("", 1, out);
	else
		return fwrite_all(s, strlen(s) + 1, out);
}

static int
fwrite_all(const void *data, size_t n, FILE *out)
{
	if (fwrite(data, n, 1, out) != 1)
		return -1;
	return 0;
}

static int
letter_cmp_path(const void *one, const void *two)
{
	const struct letter *n1 = one, *n2 = two;
	return strcmp(n1->path, n2->path);
}

void
cache_free(struct cache *cache)
{
	for (size_t i = 0; i < cache->nentry; i++) {
		if (cache->entries[i].date != -1)
			letter_free(&cache->entries[i]);
	}
	free(cache->entries);
}

static int
letter_search_path(const void *one, const void *two)
{
	const char *n1 = one;
	const struct letter *n2 = two;
	return strcmp(n1, n2->path);
}

struct letter *
cache_take(struct cache *cache, const char *path)
{
	struct letter *letter;

	if (cache->entries == NULL)
		return NULL;

	letter = bsearch(path, cache->entries, cache->nentry, sizeof(*cache->entries),
		letter_search_path);
	if (letter == NULL)
		return NULL;

	/* already taken */
	if (letter->date == -1)
		return NULL;

	letter->date = -1;
	return letter;
}
