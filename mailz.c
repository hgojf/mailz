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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cache.h"
#include "commands.h"
#include "conf.h"
#include "letter.h"
#include "list.h"
#include "pathnames.h"
#include "print.h"
#include "read-letters.h"
#include "send.h"
#include "setup.h"
#include "macro.h"

static void usage(void);

struct {
	const char *path;
	const char *perm;
} unveils[] = {
	/* { argv[0], "rwc" }, */
	{ PATH_LESS, "x" },
	{ PATH_MAILDIR_EXTRACT, "x" },
	{ PATH_MAILDIR_READ_LETTER, "x" },
	{ PATH_SENDMAIL, "x" },
	{ PATH_TMPDIR, "rwc" },
};

static int cache_write1(int, int, struct cache_conf *, struct letter *,
	size_t);

int
main(int argc, char *argv[])
{
	struct mailz_conf conf;
	struct letter *letters;
	FILE *config_fp;
	size_t nletter;
	int ch, cur, root, rv, view_all;

	rv = 1;

	view_all = 0;
	while ((ch = getopt(argc, argv, "as:")) != -1) {
		switch (ch) {
		case 'a':
			view_all = 1;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		err(1, "setlocale");
	if (mkdir(PATH_TMPDIR, 0700) == -1 && errno != EEXIST)
		err(1, "mkdir %s", PATH_TMPDIR);
	if ((config_fp = config_file()) == NULL && errno != ENOENT)
		goto tmpdir;

	for (size_t i = 0; i < nitems(unveils); i++) {
		if (unveil(unveils[i].path, unveils[i].perm) == -1) {
			warn("%s", unveils[i].path);
			goto config_fp;
		}
	}

	if (unveil(argv[0], "rwc") == -1) {
		warn("%s", argv[0]);
		goto config_fp;
	}

	if (pledge("stdio rpath wpath cpath flock sendfd proc exec unveil", NULL) == -1)
		err(1, "pledge");

	if (config_fp != NULL) {
		if (config_init(&conf, config_fp, "") == -1) {
			warnx("configuration failed");
			goto config_fp;
		}
	}
	else
		config_default(&conf);

	if ((root = open(argv[0], 
		O_RDONLY | O_DIRECTORY | O_CLOEXEC)) == -1) {
			warn("%s", argv[0]);
			goto config;
	}

	if ((cur = openat(root, "cur", 
		O_RDONLY | O_DIRECTORY | O_CLOEXEC)) == -1) {
			warn("%s/cur", argv[0]);
			goto root;
	}

	if (maildir_setup(root, cur) == -1)
		goto cur;

	signal(SIGPIPE, SIG_IGN);
	if (letters_read(root, cur, view_all, &letters, &nletter) == -1) {
		warnx("failed to read letters");
		goto cur;
	}

	if (nletter == 0) {
		puts("No mail.");
		goto good;
	}

	if (commands_run(cur, letters, nletter, &conf) == -1)
		goto letters;

	if (conf.cache.enabled) {
		if (cache_write1(root, view_all, &conf.cache, letters, 
			nletter) == -1)
				goto letters;
	}

	good:
	rv = 0;
	letters:
	for (size_t i = 0; i < nletter; i++)
		letter_free(&letters[i]);
	free(letters);
	cur:
	close(cur);
	root:
	close(root);
	config:
	config_free(&conf);
	config_fp:
	if (config_fp != NULL)
		fclose(config_fp);
	tmpdir:
	if (rmdir(PATH_TMPDIR) == -1 && errno != ENOTEMPTY)
		warn("rmdir");
	return rv;
}

static int
cache_write1(int root, int view_all, struct cache_conf *cache,
	struct letter *letters, size_t nletter)
{
	struct flock flock;
	FILE *fp;
	int fd;

	if (nletter < cache->min)
		return 0;

	if ((fd = openat(root, ".mailzcache", O_WRONLY | O_TRUNC | O_CREAT,
		0600)) == -1)
			return -1;

	if ((fp = fdopen(fd, "w")) == NULL) {
		close(fd);
		return -1;
	}

	flock.l_start = 0;
	flock.l_len = 0;
	flock.l_pid = getpid();
	flock.l_type = F_WRLCK;
	flock.l_whence = SEEK_SET;
	if (fcntl(fd, F_SETLKW, &flock) == -1)
		goto fp;

	if (cache_write(view_all, cache->max, fp, letters, nletter) == -1)
		goto fp;
	if (fflush(fp) == EOF)
		goto fp;

	fclose(fp);
	return 0;

	fp:
	unlinkat(root, ".mailzcache", 0);
	fclose(fp);
	return -1;
}

static void
usage(void)
{
	fprintf(stderr, "usage: mailz [-a] mailbox\n"
					"       mailz [-s subject] to-addr...\n");
	exit(2);
}
