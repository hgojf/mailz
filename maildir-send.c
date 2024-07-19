#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "edit.h"
#include "maildir-send.h"
#include "pathnames.h"

static int sendmail(int);
static int vi(const char *);

int
maildir_send(enum edit_mode em, const char *from, const char *subject, 
	const char *to, FILE *seed)
{
	char path[] = PATH_TMPDIR "send.XXXXXX";
	FILE *fp;
	int c, fd, rv;

	rv = -1;

	if ((fd = mkstemp(path)) == -1)
		return -1;
	if ((fp = fdopen(fd, "w")) == NULL) {
		(void) close(fd);
		return -1;
	}
	if (fprintf(fp, "From: %s\n", from) < 0)
		goto fail;
	if (fprintf(fp, "To: %s\n", to) < 0)
		goto fail;
	if (subject != NULL)
		if (fprintf(fp, "Subject: %s\n", subject) < 0)
			goto fail;
	if (fputc('\n', fp) == EOF)
		goto fail;

	if (seed != NULL) {
		while ((c = fgetc(seed)) != EOF)
			if (fputc(c, fp) == EOF)
				goto fail;
		if (ferror(seed))
			goto fail;
	}

	switch (em) {
		case EDIT_MODE_CAT:
			while ((c = fgetc(stdin)) != EOF)
				if (fputc(c, fp) == EOF)
					goto fail;
			if (ferror(stdin))
				goto fail;
			break;
		case EDIT_MODE_MANUAL:
			if (fflush(fp) == EOF)
				goto fail;
			if (printf("message located at %s, press enter after editing"
					" or q to cancel\n", path) < 0)
				goto fail;


			if ((c = fgetc(stdin)) == EOF)
				goto fail;
			if (c != '\n') {
				while ((c = fgetc(stdin)) != EOF && c != '\n')
					;
				goto good;
			}
			break;
		case EDIT_MODE_VI:
			if (fflush(fp) == EOF)
				goto fail;
			if (vi(path) == -1)
				goto fail;
			break;
	}

	if (fflush(fp) == EOF)
		goto fail;

	if (sendmail(fd) == -1)
		goto fail;

	good:
	rv = 0;
	fail:
	if (fclose(fp) == EOF)
		rv = -1;
	if (unlink(path) == -1)
		rv = -1;
	return rv;
}

static int
sendmail(int fd)
{
	pid_t pid;
	int status;

	switch (pid = fork()) {
	case -1:
		return -1;
	case 0:
		if (dup2(fd, STDIN_FILENO) == -1)
			err(1, "dup2");
		if (lseek(STDIN_FILENO, 0, SEEK_SET) == -1)
			err(1, "lseek");
		execl(PATH_SENDMAIL, "sendmail", "-t", NULL);
		err(1, "execl");
	}

	if (waitpid(pid, &status, 0) == -1)
		return -1;
	if (status != 0)
		return -1;
	return 0;
}

static int
vi(const char *path)
{
	pid_t pid;
	int status;

	switch (pid = fork()) {
	case -1:
		return -1;
	case 0:
		execl(PATH_VI, "vi", "--", path, NULL);
		err(1, "execl");
	default:
		break;
	}

	if (waitpid(pid, &status, 0) == -1)
		return -1;
	if (status != 0)
		return -1;
	return 0;
}
