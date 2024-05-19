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

static void usage(void);
static int letter_print(size_t, struct letter *);
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

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
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
			if (maildir_read(cur, &mail) == -1)
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
				fputs(mail.letters[nth - 1].text, stdout);
			}
		}
	}

	free(line);
	close(fd);
	mail_free(&mail);
	assert(getdtablecount() == 3);
}

static int
mail_print(struct mail *mail, size_t b, size_t e)
{
	for (size_t i = 0; b < e; i++, b++) {
		if (letter_print(i, &mail->letters[b]) == -1)
			return -1;
	}
	return 0;
}

static int
letter_print(size_t nth, struct letter *letter)
{
	const char *subject, *from;
	int n;

	if ((from = header_find(letter, "From")) == NULL)
		from = "Unknown sender";
	if ((subject = header_find(letter, "Subject")) == NULL)
		subject = "No subject";
	n = strlen(subject);
	return printf("%zu    %s %s\n", nth, from, subject);
}

static void
usage(void)
{
	fprintf(stderr, "using: mailz <mailbox>\n");
	exit(1);
}
