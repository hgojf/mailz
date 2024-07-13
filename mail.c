#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "address.h"
#include "argv.h"
#include "letter.h"
#include "config.h"
#include "command.h"
#include "errstr.h"
#include "maildir.h"
#include "pathnames.h"

struct mailbox {
	size_t nletters;
	struct letter *letters;
};

static void config_free(struct config *);
static int read_maildir(const char *, int, int, struct mailbox *);
static int send_mail(const char *, const char *);
static int setup_maildir(const char *, int);
static __dead void usage(void);

int
main(int argc, char *argv[])
{
	struct mailbox mailbox;
	struct config config;
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

	if (subject != NULL) {
		if (view_all)
			usage();
		return send_mail(subject, argv[0]);
	}

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		err(1, "setlocale");

	if ((dev_null = open(PATH_DEV_NULL, O_RDWR)) == -1)
		err(1, "%s", PATH_DEV_NULL);

	if (configure(&config) == -1)
		exit(1);

	rv = 1;

	if (mkdir(PATH_TMPDIR, 0700) == -1 && errno != EEXIST)
		warn("mkdir");

	if (unveil(PATH_LESS, "x") == -1)
		err(1, "unveil");
	if (unveil(PATH_MAILDIR_READ, "x") == -1)
		err(1, "unveil");
	if (unveil(PATH_MAILDIR_READ_LETTER, "x") == -1)
		err(1, "unveil");
	if (unveil(PATH_MAILDIR_SETUP, "x") == -1)
		err(1, "unveil");
	if (unveil(PATH_MAILDIR_SEND, "x") == -1)
		err(1, "unveil");
	if (unveil(PATH_TMPDIR, "crw") == -1)
		err(1, "unveil");
	if (pledge("stdio proc exec rpath cpath wpath", NULL) == -1)
		err(1, "pledge");

	signal(SIGPIPE, SIG_IGN);

	if (setup_maildir(argv[0], dev_null) == -1)
		goto config;
	if (read_maildir(argv[0], view_all, dev_null, &mailbox) == -1)
		goto config;

	if (command(&config, mailbox.letters, mailbox.nletters, argv[0], dev_null) == -1)
		goto config;

	rv = 0;
	if (rmdir(PATH_TMPDIR) == -1 && errno != ENOTEMPTY) {
		warn("rmdir %s", PATH_TMPDIR);
		rv = 1;
	}
	config:
	config_free(&config);
	dev_null:
	if (close(dev_null) == -1)
		rv = 1;
	return rv;
}

static int
send_mail(const char *subject, const char *to)
{
	struct config config;
	struct maildir_send mdse;
	int dev_null, rv;

	rv = 1;

	if ((dev_null = open(PATH_DEV_NULL, O_RDONLY)) == -1)
		err(1, "%s", PATH_DEV_NULL);
	if (configure(&config) == -1)
		goto dev_null; /* error message done by function itself */

	if (unveil(PATH_MAILDIR_SEND, "x") == -1)
		err(1, "unveil");
	if (pledge("stdio proc exec", NULL) == -1)
		err(1, "pledge");

	if (config.address.addr == NULL) {
		warnx("must set an email address");
		goto config;
	}

	mdse = maildir_send(config.address.addr, to, subject, STDIN_FILENO, dev_null);
	if (mdse.status != 0) {
		if (mdse.save_errno != 0)
			warnc(mdse.save_errno, "%s", maildir_send_errstr(mdse.status));
		else
			warnx("%s", maildir_send_errstr(mdse.status));
		goto config;
	}

	rv = 0;
	config:
	config_free(&config);
	dev_null:
	if (close(dev_null) == -1) {
		warn("close");
		rv = 1;
	}
	return rv;
}

static void
config_free(struct config *cfg)
{
	free(cfg->address.addr);
	free(cfg->address.name);

	for (size_t i = 0; i < cfg->reorder.argv.argc; i++)
		free(cfg->reorder.argv.argv[i]);
	free(cfg->reorder.argv.argv);
	if (cfg->reorder.shm.fd != -1)
		(void) close(cfg->reorder.shm.fd);

	for (size_t i= 0; i < cfg->ignore.argv.argc; i++)
		free(cfg->ignore.argv.argv[i]);
	free(cfg->ignore.argv.argv);
	if (cfg->ignore.shm.fd != -1)
		(void) close(cfg->ignore.shm.fd);
}

static int
read_maildir(const char *path, int show_all, int dev_null, struct mailbox *out)
{
	struct maildir_read mdr;

	mdr = maildir_read(path, dev_null);
	if (mdr.status != 0) {
		warnx("maildir_read");
		return -1;
	}

	out->nletters = mdr.val.good.nletters;
	out->letters = mdr.val.good.letters;
	return 0;
}

static int
setup_maildir(const char *path, int dev_null)
{
	struct maildir_setup mds;

	mds = maildir_setup(path, dev_null);
	if (mds.status != 0) {
		if (mds.save_errno != 0)
			warnc(mds.save_errno, "%s", maildir_setup_errstr(mds.status));
		else
			warnx("%s (%d)", maildir_setup_errstr(mds.status), mds.status);
		return -1;
	}
	return 0;
}

static __dead void
usage(void)
{
	fprintf(stderr, "usage: mailz [-a] mailbox\n"
					"       mailz [-s subject] to-addr\n");
	exit(1);
}
