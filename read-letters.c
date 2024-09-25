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
#include <sys/time.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cache.h"
#include "extract.h"
#include "letter.h"
#include "maildir.h"
#include "read-letters.h"
#include "macro.h"

#define HEADER_DATE 0
#define HEADER_FROM 1
#define HEADER_SUBJECT 2

static int
cache_read1(int root, struct cache *cache, struct timespec *mtim)
{
	struct stat sb;
	struct flock flock;
	FILE *fp;
	int fd, rv;

	rv = -1;

	if ((fd = openat(root, ".mailzcache", O_RDONLY)) == -1) {
		if (errno == ENOENT)
			return 0;
		return -1;
	}

	if ((fp = fdopen(fd, "r")) == NULL) {
		close(fd);
		return -1;
	}

	if (fstat(fd, &sb) == -1)
		goto fp;

	flock.l_start = 0;
	flock.l_len = 0;
	flock.l_pid = getpid();
	flock.l_type = F_RDLCK;
	flock.l_whence = SEEK_SET;
	if (fcntl(fd, F_SETLKW, &flock) == -1)
		goto fp;

	switch (cache_read(fp, cache)) {
	case -1:
		goto fp;
	case -2: /* no cache */
		fclose(fp);
		return 0;
	default:
		break;
	}

	*mtim = sb.st_mtim;
	rv = 1;
	fp:
	fclose(fp);
	return rv;
}

static int
letter_cmp_date(const void *one, const void *two)
{
	const struct letter *n1 = one, *n2 = two;

	if (n1->date > n2->date)
		return 1;
	else if (n1->date == n2->date)
		return 0;
	else
		return -1;
}

/* takes ownership of fd regardless of return value */
static int
letter_read(struct extract *extract, int fd, const char *path,
	struct extracted_header *headers, size_t nh, struct letter *out)
{
	if (maildir_extract_next(extract, fd, headers, nh) == -1)
		return -1;

	if (headers[HEADER_DATE].val.date == -1)
		goto headers;
	if (headers[HEADER_FROM].val.from.addr == NULL)
		goto headers;

	if ((out->path = strdup(path)) == NULL)
		goto headers;
	out->date = headers[HEADER_DATE].val.date;
	out->from.addr = headers[HEADER_FROM].val.from.addr;
	out->from.name = headers[HEADER_FROM].val.from.name;
	if (strlen(headers[HEADER_SUBJECT].val.string) != 0)
		out->subject = headers[HEADER_SUBJECT].val.string;
	else {
		free(headers[HEADER_SUBJECT].val.string);
		out->subject = NULL;
	}

	return 0;

	headers:
	for (size_t i = 0; i < nh; i++)
		extract_header_free(headers[i].type, &headers[i].val);
	return -1;
}

int
letters_read(int root, int cur, int view_seen, 
	struct letter **out_letters, size_t *out_nletter)
{
	struct cache cache;
	struct extract extract;
	struct extracted_header headers[3];
	struct letter *letters;
	struct stat sb;
	struct timespec cache_mtim;
	DIR *dp;
	size_t nletter;
	int cv, curfd;

	if (fstat(cur, &sb) == -1)
		return -1;

	if ((cv = cache_read1(root, &cache, &cache_mtim)) == -1)
		return -1;
	if (cv == 0) /* no cache */
		memset(&cache, 0, sizeof(cache));

	if (cv != 0 && cache.view_all == view_seen
		&& timespeccmp(&sb.st_mtim, &cache_mtim, <=)) {
			/* fully up to date, just take the cache and sort it */
			qsort(cache.letters, cache.nletter, sizeof(*cache.letters), 
				letter_cmp_date);
			*out_letters = cache.letters;
			*out_nletter = cache.nletter;
			return 0;
	}

	if ((curfd = dup(cur)) == -1)
		goto cache;
	if (fcntl(curfd, F_SETFD, 1) == -1) {
		close(curfd);
		goto cache;
	}

	if ((dp = fdopendir(curfd)) == NULL) {
		close(curfd);
		goto cache;
	}

	headers[HEADER_DATE].key = "Date";
	headers[HEADER_DATE].type = EXTRACT_DATE;

	headers[HEADER_FROM].key = "From";
	headers[HEADER_FROM].type = EXTRACT_FROM;

	headers[HEADER_SUBJECT].key = "Subject";
	headers[HEADER_SUBJECT].type = EXTRACT_STRING;

	if (maildir_extract(&extract, headers, nitems(headers)) == -1)
		goto cur;

	letters = NULL;
	nletter = 0;
	for (;;) {
		struct dirent *de;
		struct letter letter, *t;

		errno = 0;
		if ((de = readdir(dp)) == NULL) {
			if (errno == 0)
				break;
			goto letters;
		}

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;

		if (!view_seen && maildir_letter_seen(de->d_name))
			continue;

		if (cache_take(&cache, de->d_name, &letter) == -1) {
			int fd;

			if ((fd = openat(curfd, de->d_name, O_RDONLY)) == -1)
				goto letters;

			if (letter_read(&extract, fd, de->d_name, headers, nitems(headers), 
				&letter) == -1)
					goto letters;
		}

		t = reallocarray(letters, nletter + 1, sizeof(*letters));
		if (t == NULL) {
			letter_free(&letter);
			goto letters;
		}
		letters = t;
		letters[nletter++] = letter;
	}

	maildir_extract_close(&extract);
	closedir(dp);
	cache_free(&cache);

	qsort(letters, nletter, sizeof(*letters), 
		letter_cmp_date);
	*out_letters = letters;
	*out_nletter = nletter;
	return 0;

	letters:
	for (size_t i = 0; i < nletter; i++)
		letter_free(&letters[i]);
	free(letters);
	maildir_extract_close(&extract);
	cur:
	closedir(dp);
	cache:
	cache_free(&cache);
	return -1;
}
