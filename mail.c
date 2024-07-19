#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "address.h"
#include "argv.h"
#include "edit.h"
#include "letter.h"
#include "config.h"
#include "command.h"
#include "maildir.h"
#include "maildir-cache-write.h"
#include "maildir-send.h"
#include "pathnames.h"

struct mailbox {
	size_t nletters;
	struct letter *letters;
	int cur;
};

static void config_free(struct config *);
static int read_maildir(char *, int, int, int *, struct mailbox *);
static int send_mail(const char *, const char *);
static __dead void usage(void);
static int write_cache(struct mailbox *, int);

int
main(int argc, char *argv[])
{
	struct mailbox mailbox;
	struct config config;
	int ch, dev_null, need_recache, rv, view_all;
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
	if (unveil(PATH_SENDMAIL, "x") == -1)
		err(1, "unveil");
	if (unveil(PATH_TMPDIR, "crw") == -1)
		err(1, "unveil");
	if (unveil(PATH_VI, "x") == -1)
		err(1, "unveil");
	if (unveil(argv[0], "rwc") == -1)
		err(1, "unveil");
	if (pledge("stdio proc exec rpath cpath wpath flock", NULL) == -1)
		err(1, "pledge");

	signal(SIGPIPE, SIG_IGN);

	if (maildir_setup(argv[0], dev_null) == -1)
		goto config;
	if (read_maildir(argv[0], view_all, dev_null, &need_recache, &mailbox) == -1)
		goto config;

	if (mailbox.nletters == 0) {
		puts("No mail.");
		goto good;
	}

	if (command(&config, mailbox.letters, mailbox.nletters, argv[0], dev_null,
			mailbox.cur) == -1)
		goto config;

	if (need_recache) {
		if (write_cache(&mailbox, view_all) == -1) {
			warnx("failed to write cache");
			goto mailbox;
		}
	}

	good:
	rv = 0;
	if (rmdir(PATH_TMPDIR) == -1 && errno != ENOTEMPTY) {
		warn("rmdir %s", PATH_TMPDIR);
		rv = 1;
	}
	mailbox:
	for (size_t i = 0; i < mailbox.nletters; i++) {
		free(mailbox.letters[i].from.str);
		free(mailbox.letters[i].path);
		free(mailbox.letters[i].subject);
	}
	free(mailbox.letters);
	if (close(mailbox.cur) == -1) {
		warn("close");
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
	int dev_null, rv;

	rv = 1;

	if ((dev_null = open(PATH_DEV_NULL, O_RDONLY)) == -1)
		err(1, "%s", PATH_DEV_NULL);
	if (configure(&config) == -1)
		goto dev_null; /* error message done by function itself */

	if (mkdir(PATH_TMPDIR, 0700) == -1 && errno != EEXIST)
		goto config;
	if (unveil(PATH_SENDMAIL, "x") == -1)
		err(1, "unveil");
	if (unveil(PATH_TMPDIR, "rwc") == -1)
		err(1, "unveil");
	if (pledge("stdio proc exec rpath wpath cpath", NULL) == -1)
		err(1, "pledge");

	if (config.address.addr == NULL) {
		warnx("must set an email address");
		goto tmp;
	}

	if (maildir_send(EDIT_MODE_CAT, config.address.addr, subject, to) == -1)
		goto tmp;

	rv = 0;
	tmp:
	if (rmdir(PATH_TMPDIR) == -1 && errno != ENOTEMPTY) {
		warn("rmdir %s", PATH_TMPDIR);
		rv = 1;
	}
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
write_cache(struct mailbox *mailbox, int view_all)
{
	int fd, rv;
	FILE *fp;

	fd = openat(mailbox->cur, "../.mailzcache", O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1)
		return -1;
	if ((fp = fdopen(fd, "w")) == NULL) {
		(void) close(fd);
		return -1;
	}

	if (flock(fd, LOCK_EX) == -1) {
		(void) fclose(fp);
		return -1;
	}

	rv = maildir_cache_write(fp, mailbox->letters, mailbox->nletters, view_all);

	if (flock(fd, LOCK_UN) == -1) {
		(void) fclose(fp);
		return -1;
	}
	if (fclose(fp) == EOF)
		return -1;

	return rv;
}

static int
read_maildir(char *root, int show_all, int dev_null, int *need_recache,
	struct mailbox *out)
{
	char path[PATH_MAX];
	struct maildir_read mdr;
	int n;

	n = snprintf(path, sizeof(path), "%s/cur", root);
	if (n < 0) {
		warn("snprintf");
		return -1;
	}
	else if (n >= sizeof(path)) {
		warnc(ENAMETOOLONG, "%s/cur", root);
		return -1;
	}

	if ((out->cur = open(path, O_RDONLY | O_DIRECTORY)) == -1) {
		warn("%s", path);
		return -1;
	}

	mdr = maildir_read(root, dev_null, show_all);
	if (mdr.status != 0) {
		(void) close(out->cur);
		warnx("maildir_read");
		return -1;
	}

	out->nletters = mdr.val.good.nletters;
	out->letters = mdr.val.good.letters;
	*need_recache = mdr.val.good.need_recache;
	return 0;
}

static __dead void
usage(void)
{
	fprintf(stderr, "usage: mailz [-a] mailbox\n"
					"       mailz [-s subject] to-addr\n");
	exit(1);
}
