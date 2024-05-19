#include <sys/stat.h>
#include <sys/tree.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mail.h"
#include "mail-util.h"
#include "maildir.h"
#include "commands.h"

static int configure(struct options *);
static int header_ignore(struct header *, const struct options *);
static void usage(void);
static int letter_print(size_t, struct letter *);
static int letter_print_read(struct letter *, const struct options *);
static int mail_print(struct mail *, size_t, size_t);

#define max(a, b) ((a) > (b) ? (a) : (b))

int
main(int argc, char *argv[])
{
	int ch, fd;
	struct stat sb;
	struct mail mail;
	DIR *cur;
	char *line = NULL;
	size_t n = 0;
	ssize_t len;
	struct options options;
	const char *cfg;

	options.view_seen = 0;
	options.nignore = 0;
	options.ignore = NULL;
	options.nunignore = 0;
	options.unignore = NULL;

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
	if ((cfg = getenv("MAILZRC")) != NULL && unveil(cfg, "r") == -1)
		err(1, "unveil");
	if (pledge("stdio rpath cpath", NULL) == -1)
		err(1, "pledge");

	if ((fd = open(argv[0], O_RDONLY)) == -1)
		err(1, "open %s", argv[0]);
	if (fstat(fd, &sb) == -1) {
		warn("fstat");
		close(fd);
		exit(1);
	}

	switch (sb.st_mode & S_IFMT) {
		case S_IFDIR: /* assume maildir */
			if ((cur = maildir_setup(fd)) == NULL)
				err(1, "maildir_setup");
			if (pledge("stdio rpath", NULL) == -1)
				err(1, "maildir_setup");
			/* mailread_read takes owner of 'cur' */
			if (maildir_read(cur, &mail, &options) == -1)
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

	configure(&options);
	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	mail_print(&mail, 0, mail.nletters);

	while ((len = getline(&line, &n, stdin)) != -1) {
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		if (isdigit(*line)) {
			size_t nth;
			const char *errstr;

			nth = strtonum(line, 1, mail.nletters, &errstr);
			if (errstr != NULL)
				warnx("Message number was %s", errstr);
			else {
				letter_print_read(&mail.letters[nth - 1], &options);
			}
		}
		else {
			command_run(line, &options);
		}
	}

	free(line);
	close(fd);
	mail_free(&mail);
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

static int
configure(struct options *options)
{
	const char *mailrc;
	FILE *fp;
	char *line = NULL;
	size_t n = 0;
	ssize_t len;
	int rv;

	if ((mailrc = getenv("MAILZRC")) == NULL)
		return 0;
	if ((fp = fopen(mailrc, "r")) == NULL) {
		warn("fopen %s", mailrc);
		return -1;
	}

	while ((len = getline(&line, &n, fp)) != -1) {
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		command_run(line, options);
	}

	rv = ferror(fp) ? -1 : 0;
	free(line);
	fclose(fp);
	return rv;
}

static int
letter_print_read(struct letter *letter, const struct options *options)
{
	struct header *h;

	RB_FOREACH(h, headers, &letter->headers) {
		if (header_ignore(h, options))
			continue;
		if (printf("%s: %s\n", h->key, h->val) < 0)
			return -1;
		skip:
		continue;
	}
	if (fputs(letter->text, stdout) == EOF)
		return -1;
	return 0;
}

static int
header_ignore(struct header *header, const struct options *options)
{
	if (options->nunignore != 0) {
		for (size_t i = 0; i < options->nunignore; i++) {
			if (!strcmp(header->key, options->unignore[i]))
				return 0;
		}
		return 1;
	}
	else {
		for (size_t i = 0; i < options->nignore; i++) {
			if (!strcmp(header->key, options->ignore[i]))
				return 1;
		}
		return 0;
	}
}

static int
mail_print(struct mail *mail, size_t b, size_t e)
{
	for (size_t i = 0; b < e; i++, b++) {
		if (letter_print(i + 1, &mail->letters[b]) == -1)
			return -1;
	}
	return 0;
}

static int
letter_print(size_t nth, struct letter *letter)
{
	const char *subject, *from;
	char date[30];
	struct tm *tm;

	if ((tm = localtime(&letter->sent)) == NULL) {
		strlcpy(date, "Unknown date", sizeof(date));
	}
	else {
		if (strftime(date, sizeof(date), "%a %b %d %H:%M", tm) == 0)
			strlcpy(date, "Unknown date", sizeof(date));
	}

	if ((from = header_find(letter, "From")) == NULL)
		from = "Unknown sender";
	if ((subject = header_find(letter, "Subject")) == NULL)
		subject = "No subject";
	return printf("%zu    %s %s %s\n", nth, date, from, subject);
}

static void
usage(void)
{
	fprintf(stderr, "usage: mailz [-s] <mailbox>\n");
	exit(1);
}
