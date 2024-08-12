#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "conf.h"
#include "leak.h"
#include "letter.h"
#include "cache.h"
#include "commands.h"
#include "pathnames.h"
#include "read-letters.h"
#include "send.h"
#include "setup.h"

static struct {
	const char *path;
	const char *perm;
} unveils[] = {
	/* { argv[0], "rwc" }, */
	{ PATH_TMPDIR, "rwc" },
	{ PATH_LESS, "x"} ,
	{ PATH_MAILDIR_READ, "x" },
	{ PATH_MAILDIR_READ_LETTER, "x" },
	{ PATH_SENDMAIL, "x" },
};

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

static __dead void usage(void);

int dev_null;

int
main(int argc, char *argv[])
{
	struct mailz_conf conf;
	struct mailbox mbox;
	const char *subject;
	int ch, cur, root, rv, view_all;

	rv = 1;

	subject = NULL;
	view_all = 0;
	while ((ch = getopt(argc, argv, "as:")) != -1) {
		switch (ch) {
		case 'a':
			view_all = 1;
			break;
		case 's':
			subject = optarg;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	signal(SIGPIPE, SIG_IGN);
	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		err(1, "setlocale");

	if (argc != 1)
		usage();

	if (configure(&conf) == -1)
		exit(1); /* configure reports error */

	if (subject != NULL) {
		struct sendmail_subject sms;
		struct sendmail_from from, to;
		FILE *fp;
		int c;

		if (conf.address.addr == NULL)
			errx(1, "must set a sending address");

		sms.s = subject;
		sms.reply = 0;

		from.addr = conf.address.addr;
		from.name = conf.address.name;

		to.addr = argv[0];
		to.name = NULL;

		if ((fp = tmpfile()) == NULL)
			err(1, NULL);

		if (sendmail_setup(&sms, &from, &to, NULL, 0, fp) == -1)
			err(1, "sendmail_setup");

		while ((c = fgetc(stdin)) != EOF) {
			if (fputc(c, fp) == EOF)
				err(1, "fputc");
		}

		if (fflush(fp) == EOF)
			err(1, "fflush");

		if (sendmail_send(fp) == -1)
			err(1, "sendmail_send");

		fclose(fp);
		config_free(&conf);
		return 0;
	}

	if ((dev_null = open(PATH_DEV_NULL, O_RDONLY | O_CLOEXEC)) == -1)
		err(1, "%s", PATH_DEV_NULL);

	if (mkdir(PATH_TMPDIR, 0700) == -1 && errno != EEXIST)
		err(1, "%s", PATH_TMPDIR);

	for (size_t i = 0; i < nitems(unveils); i++) {
		if (unveil(unveils[i].path, unveils[i].perm) == -1) {
			warn("%s", unveils[i].path);
			goto tmpdir;
		}
	}

	if (unveil(argv[0], "rwc") == -1) {
		warn("%s", argv[0]);
		goto tmpdir;
	}

	if (pledge("stdio rpath cpath wpath sendfd proc exec", NULL) == -1)
		err(1, "pledge");

	if ((root = open(argv[0], O_RDONLY | O_CLOEXEC)) == -1) {
		warn("%s", argv[0]);
		goto tmpdir;
	}
	if ((cur = openat(root, "cur", O_RDONLY | O_CLOEXEC)) == -1) {
		warn("%s/cur", argv[0]);
		goto root;
	}

	if (maildir_setup(argv[0], root, cur) == -1)
		goto cur; /* maildir_setup reports error */
	if (maildir_read(cur, conf.cache, view_all, &mbox.letters, 
			&mbox.nletter) == -1) {
		warn("maildir_read");
		goto cur;
	}

	if (mbox.nletter == 0) {
		puts("No mail.");
		goto good;
	}

	for (size_t i = 0; i < mbox.nletter; i++)
		letter_print(i + 1, &mbox.letters[i]);

	commands_run(cur, &mbox, &conf);

	if (conf.cache && cache_write(root, view_all, mbox.letters, mbox.nletter) == -1) {
		warn("cache_write");
		goto cur;
	}

	good:
	rv = 0;
	close(dev_null);
	cur:
	close(cur);
	root:
	close(root);
	tmpdir:
	if (rmdir(PATH_TMPDIR) == -1 && errno != ENOTEMPTY) {
		warn("rmdir %s", PATH_TMPDIR);
		rv = 1;
	}
	config_free(&conf);

	if (fd_leak_report())
		rv = 1;
	return rv;
}

static void
usage(void)
{
	fprintf(stderr, "usage: mailz [-a] mailbox\n"
					"       mailz [-s subject] to-addr\n");
	exit(2);
}
