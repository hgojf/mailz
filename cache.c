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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cache.h"
#include "letter.h"
#include "maildir.h"
#include "printable.h"

#define MAILZCACHE_MAGIC_MASK	0xFFFFFF00
#define MAILZCACHE_MAGIC		0x48414D00
#define MAILZCACHE_VERSION_MASK	0x000000FF
#define MAILZCACHE_VERSION		0x00000002

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
 * subject (optional printable nul terminated string)
 *
 * For optional strings, an empty string signifies no data.
 * All integers are in little endian format
 */

struct getline {
	char *line;
	size_t n;
};

static int fread_all(void *, size_t, FILE *);
static int fread_letter(struct letter *, struct getline *, FILE *);
static int fread_string(char **, struct getline *, FILE *, int);
static int fread_string_opt(char **, struct getline *, FILE *);
static int fwrite_all(void *, size_t, FILE *, long long *);
static int fwrite_string(char *, FILE *, long long *);
static int fwrite_string_opt(char *, FILE *, long long *);
static int letter_cmp(const void *, const void *);
static int letter_path_cmp(const void *, const void *);

void
cache_free(struct cache *cache)
{
	for (size_t i = 0; i < cache->nletter; i++) {
		if (cache->letters[i].from.addr == NULL)
			continue;
		letter_free(&cache->letters[i]);
	}
	free(cache->letters);
}

int
cache_read(FILE *fp, struct cache *cache)
{
	struct getline gl;
	struct letter *letters;
	size_t nletter;
	uint32_t version;
	uint8_t view_all;

	if (fread_all(&version, sizeof(version), fp) == -1)
		return -1;
	version = le32toh(version);

	if ((version & MAILZCACHE_MAGIC_MASK) != MAILZCACHE_MAGIC)
		return -1;
	if ((version & MAILZCACHE_VERSION_MASK) != MAILZCACHE_VERSION)
		return -2;

	if (fread_all(&view_all, sizeof(view_all), fp) == -1)
		return -1;

	memset(&gl, 0, sizeof(gl));
	letters = NULL;
	nletter = 0;

	for (;;) {
		struct letter letter, *t;
		int n;

		if ((n = fread_letter(&letter, &gl, fp)) == -1)
			goto fail;
		if (n == 0)
			break;

		t = reallocarray(letters, nletter + 1, sizeof(*letters));
		if (t == NULL) {
			letter_free(&letter);
			goto fail;
		}
		letters = t;
		letters[nletter++] = letter;

	}

	cache->letters = letters;
	cache->nletter = nletter;
	cache->view_all = (view_all & 0x1);

	free(gl.line);
	return 0;

	fail:
	free(gl.line);
	for (size_t i = 0; i < nletter; i++)
		letter_free(&letters[i]);
	free(letters);
	return -1;
}

/* 
 * It is the responsibility of the caller to free the contents of the 
 * returned letter, if the return value is not -1.
 * The path field of the letter must not be freed if another call to
 * cache_take is to be made.
 */
int
cache_take(struct cache *cache, const char *path, struct letter *letter)
{
	struct letter *l;

	if (cache->nletter == 0)
		return -1;

	l = bsearch(path, cache->letters, cache->nletter, 
		sizeof(*cache->letters), letter_path_cmp);

	if (l == NULL || l->from.addr == NULL)
		return -1;

	*letter = *l;
	l->from.addr = NULL;
	return 0;
}

int
cache_write(int view_all, long long max, FILE *fp, 
	struct letter *letters, size_t nl)
{
	uint32_t version;
	uint8_t view_all1;

	version = MAILZCACHE_MAGIC | MAILZCACHE_VERSION;
	version = htole32(version);

	view_all1 = view_all;

	if (fwrite_all(&version, sizeof(version), fp, &max) == -1)
		return -1;
	if (fwrite_all(&view_all1, sizeof(view_all1), fp, &max) == -1)
		return -1;

	qsort(letters, nl, sizeof(*letters), letter_cmp);

	for (size_t i = 0; i < nl; i++) {
		int64_t date;

		if (!view_all && maildir_letter_seen(letters[i].path))
			continue;

		date = letters[i].date;
		date = (int64_t)htole64(date);

		if (fwrite_all(&date, sizeof(date), fp, &max) == -1)
			return -1;

		if (fwrite_string(letters[i].from.addr, fp, &max) == -1)
			return -1;

		if (fwrite_string_opt(letters[i].from.name, fp, &max) == -1)
			return -1;

		if (fwrite_string(letters[i].path, fp, &max) == -1)
			return -1;

		if (fwrite_string_opt(letters[i].subject, fp, &max) == -1)
			return -1;
	}

	return 0;
}

static int
fread_all(void *data, size_t n, FILE *fp)
{
	if (fread(data, n, 1, fp) != 1)
		return -1;
	return 0;
}

static int
fread_letter(struct letter *letter, struct getline *gl, FILE *fp)
{
	char *addr, *name, *path, *subject;
	time_t date;

	if (fread(&date, sizeof(date), 1, fp) == 0)
		return 0;
	date = (int64_t)le64toh(date);

	/* avoid errors later */
	if (localtime(&date) == NULL)
		return -1;

	if (fread_string(&addr, gl, fp, 1) == -1)
		return -1;
	if (fread_string_opt(&name, gl, fp) == -1)
		goto addr;

	if (fread_string(&path, gl, fp, 0) == -1)
		goto name;

	if (fread_string_opt(&subject, gl, fp) == -1)
		goto path;

	letter->date = (time_t)date;
	letter->from.addr = addr;
	letter->from.name = name;
	letter->path = path;
	letter->subject = subject;
	return 1;

	path:
	free(path);
	name:
	free(name);
	addr:
	free(addr);
	return -1;
}

static int
fread_string(char **out, struct getline *gl, FILE *fp, int isprint)
{
	ssize_t len;

	if ((len = getdelim(&gl->line, &gl->n, '\0', fp)) == -1)
		return -1;
	if (gl->line[len - 1] != '\0')
		return -1;

	if (isprint && !string_isprint(gl->line))
		return -1;

	if ((*out = strdup(gl->line)) == NULL)
		return -1;

	return 0;
}

static int
fread_string_opt(char **out, struct getline *gl, FILE *fp)
{
	ssize_t len;

	if ((len = getdelim(&gl->line, &gl->n, '\0', fp)) == -1)
		return -1;
	if (gl->line[len - 1] != '\0')
		return -1;

	if (len == 1) {
		*out = NULL;
		return 0;
	}

	if (!string_isprint(gl->line))
		return -1;

	if ((*out = strdup(gl->line)) == NULL)
		return -1;

	return 0;
}

static int
fwrite_all(void *data, size_t n, FILE *fp, long long *left)
{
	if (*left != -1 && n > (size_t)*left)
		return -1;

	if (fwrite(data, n, 1, fp) != 1)
		return -1;

	if (*left != -1)
		*left -= n;
	return 0;
}

static int
fwrite_string(char *s, FILE *fp, long long *left)
{
	return fwrite_all(s, strlen(s) + 1, fp, left);
}

static int
fwrite_string_opt(char *s, FILE *fp, long long *left)
{
	if (s == NULL)
		return fwrite_all("", 1, fp, left);
	else
		return fwrite_string(s, fp, left);
}

static int
letter_cmp(const void *one, const void *two)
{
	return strcmp(((const struct letter *)one)->path, 
				((const struct letter *)two)->path);
}

static int
letter_path_cmp(const void *one, const void *two)
{
	return strcmp((const char *)one,
			((const struct letter *)two)->path);
}
