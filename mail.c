#include <sys/stat.h>
#include <sys/tree.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mail.h"
#include "maildir.h"
#include "commands.h"

static int configure(struct maildir *, struct options *);
static const char *config_location(void);
static void usage(void);

#define max(a, b) ((a) > (b) ? (a) : (b))

int
main(int argc, char *argv[])
{
	int ch, fd;
	struct stat sb;
	char *line = NULL;
	size_t n = 0;
	ssize_t len;
	struct options options;
	const char *cfg;
	struct maildir maildir;

	options.view_seen = 0;
	options.nignore = 0;
	options.ignore = NULL;
	options.nunignore = 0;
	options.unignore = NULL;
	options.nreorder = 0;
	options.reorder = NULL;
	options.msg = 1;

	while ((ch = getopt(argc, argv, "s")) != -1) {
		switch (ch) {
		case 's':
			options.view_seen = 1;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	if (unveil(argv[0], "rc") == -1)
		err(1, "unveil");
	if ((cfg = config_location()) != NULL && unveil(cfg, "r") == -1)
		err(1, "unveil");
	if (unveil("/usr/local/libexec/lesswrapper", "x") == -1)
		err(1, "unveil");
	if (pledge("stdio rpath cpath proc exec", NULL) == -1)
		err(1, "pledge");

	if ((fd = open(argv[0], O_RDONLY | O_CLOEXEC)) == -1)
		err(1, "open %s", argv[0]);
	if (fstat(fd, &sb) == -1) {
		warn("fstat");
		close(fd);
		exit(1);
	}

	switch (sb.st_mode & S_IFMT) {
		case S_IFDIR: /* assume maildir */
			if (maildir_setup(fd, &maildir) == -1)
				err(1, "maildir_setup1");
			/* mailread_read takes owner of 'cur' */
			if (maildir_read(&maildir, &options) == -1)
				err(1, "maildir_read");
			break;
		case S_IFREG: /* assume mbox */
			warnx("mbox support not yet implemented");
			close(fd);
			return 1;
			/* NOTREACHED */
		default:
			warnx("mailbox is not a regular file or directory");
			close(fd);
			return 1;
			/* NOTREACHED */
	}

	if (maildir.nletters == 0) {
		puts("No mail.");
		return 0;
	}

	configure(&maildir, &options);

	maildir_print(&maildir, 0, maildir.nletters);

	fputs("> ", stdout);
	while ((len = getline(&line, &n, stdin)) != -1) {
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		if (*line == '\0') {
			if (options.msg == maildir.nletters) {
				printf("No more messages\n");
				fputs("> ", stdout);
				continue;
			}
			maildir_letter_print_read(&maildir, &maildir.letters[options.msg++ - 1],
				&options, stdout);
		}
		else if (isdigit(*line)) {
			const char *errstr;

			options.msg = strtonum(line, 1, maildir.nletters, &errstr);
			if (errstr != NULL)
				warnx("Message number was %s", errstr);
			else {
				maildir_letter_print_read(&maildir, &maildir.letters[options.msg - 1],
					&options, stdout);
			}
		}
		else {
			command_run(line, &maildir, &options);
		}
		fputs("> ", stdout);
	}

	putchar('\n');

	free(line);
	close(fd);
	maildir_free(&maildir);
	for (size_t i = 0; i < options.nreorder; i++) {
		free(options.reorder[i]);
	}
	free(options.reorder);
	for (size_t i = 0; i < options.nignore; i++) {
		free(options.ignore[i]);
	}
	free(options.ignore);
	for (size_t i = 0; i < options.nunignore; i++) {
		free(options.unignore[i]);
	}
	free(options.unignore);
	assert(getdtablecount() == 3);
}

static const char *
config_location(void)
{
	static char path[PATH_MAX];
	const char *mailrc, *home;
	static const char *ret = NULL;
	int n;

	if (ret != NULL)
		return ret;

	if ((mailrc = getenv("MAILZRC")) != NULL) {
		ret = mailrc;
		return mailrc;
	}
	if ((home = getenv("HOME")) == NULL)
		return NULL;
	n = snprintf(path, PATH_MAX, "%s/.mailzrc", home);
	if (n < 0 || n >= PATH_MAX)
		return NULL;

	ret = path;

	return ret;
}

static int
configure(struct maildir *maildir, struct options *options)
{
	const char *mailrc;
	FILE *fp;
	char *line = NULL;
	size_t n = 0;
	ssize_t len;
	int rv;

	if ((mailrc = config_location()) == NULL)
		return 0;
	if ((fp = fopen(mailrc, "re")) == NULL) {
		warn("fopen %s", mailrc);
		return -1;
	}

	while ((len = getline(&line, &n, fp)) != -1) {
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		command_run(line, maildir, options);
	}

	rv = ferror(fp) ? -1 : 0;
	free(line);
	fclose(fp);
	return rv;
}

static void
usage(void)
{
	fprintf(stderr, "usage: mailz [-s] <mailbox>\n");
	exit(1);
}
