#include <err.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "errstr.h"
#include "letter.h"
#include "maildir.h"
#include "pathnames.h"

static __dead void usage(void);

int
main(int argc, char *argv[])
{
	struct maildir_setup mds;
	struct maildir_read mdr;
	struct maildir_read_letter mdrl;
	int ch, dev_null, rv, view_all;
	const char *subject;

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

	if (argc != 1)
		usage();

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		err(1, "setlocale");

	if ((dev_null = open(PATH_DEV_NULL, O_RDWR)) == -1)
		err(1, "%s", PATH_DEV_NULL);

	if (subject != NULL) {
		struct maildir_send mdse;

		if (view_all)
			usage();

		mdse = maildir_send("user", argv[0], subject, STDIN_FILENO, dev_null);
		if (mdse.status != 0) {
			if (mdse.save_errno != 0)
				warnc(mdse.save_errno, "%s", maildir_send_errstr(mdse.status));
			else
				warnx("%s", maildir_setup_errstr(mdse.status));
			goto dev_null;
		}
		return 0;
	}

	rv = 1;


	mds = maildir_setup(argv[0], dev_null);
	if (mds.status != 0) {
		if (mds.save_errno != 0)
			warnc(mds.save_errno, "%s", maildir_setup_errstr(mds.status));
		else
			warnx("%s (%d)", maildir_setup_errstr(mds.status), mds.status);
		goto dev_null;
	}

	mdr = maildir_read(argv[0], dev_null);
	if (mdr.status != 0)
		errx(1, "maildir_read");
	printf("%zu\n", mdr.val.good.nletters);

	rv = 0;
	dev_null:
	if (close(dev_null) == -1)
		rv = 1;
	return rv;
}

static __dead void
usage(void)
{
	fprintf(stderr, "usage: mailz [-a] mailbox\n"
					"       mailz [-s subject] to-addr\n");
	exit(1);
}
