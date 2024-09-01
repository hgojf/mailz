#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "conf.h"
#include "letter.h"
#include "mark.h"
#include "pathnames.h"
#include "print.h"
#include "read-letter.h"

static int read_letter_quick(int, struct letter *, struct ignore *, 
	struct reorder *, FILE *, int, int);

int
page_letter(int cur, struct letter *letter, struct ignore *ignore,
	struct reorder *reorder, int linewrap)
{
	FILE *po;
	int p[2], status;
	pid_t pid;

	if (pipe2(p, O_CLOEXEC) == -1)
		return -1;
	if ((po = fdopen(p[1], "w")) == NULL) {
		close(p[1]);
		close(p[0]);
		return -1;
	}

	switch (pid = fork()) {
	case -1:
		fclose(po);
		close(p[0]);
		return -1;
	case 0:
		if (dup2(p[0], STDIN_FILENO) == -1)
			err(1, "dup2");
		execl(PATH_LESS, "less", NULL);
		err(1, "%s", PATH_LESS);
	default:
		break;
	}

	close(p[0]);

	switch (read_letter_quick(cur, letter, ignore, reorder, po, linewrap, 1)) {
	case -1:
		goto kill;
	case -2: /* write error */
		if (errno != EPIPE)
			goto kill;
		break;
	default:
		if (fflush(po) == EOF && errno != EPIPE)
			goto kill;
		break;
	}

	fclose(po);
	if (waitpid(pid, &status, 0) == -1 || WEXITSTATUS(status) != 0)
		return -1;

	if (mark_read(cur, letter) == -1)
		return -1;
	return 0;

	kill:
	fclose(po);
	(void)kill(pid, SIGTERM);
	(void)waitpid(pid, NULL, 0);
	return -1;
}

static int
read_letter_quick(int cur, struct letter *letter, struct ignore *ignore, 
	struct reorder *reorder, FILE *out, int linewrap, int pipeok)
{
	struct read_letter rl;
	int fd, rv;

	rv = -1;

	if ((fd = openat(cur, letter->path, O_RDONLY | O_CLOEXEC)) == -1)
		return -1;

	if (maildir_read_letter(&rl, fd, pipeok, linewrap, ignore, reorder) == -1)
		return -1;

	for (;;) {
		char buf[4];
		int n;

		if ((n = maildir_read_letter_getc(&rl, buf)) == -1)
			goto close;
		if (n == 0)
			break;

		if (fwrite(buf, n, 1, out) != 1) {
			int save_errno;

			/* 
			 * This function clobbers errno on success because fclose
			 * tries to seek on the pipe.
			 */
			save_errno = errno;
			if (maildir_read_letter_close(&rl) == -1)
				return -1;
			errno = save_errno;

			return -2;
		}
	}

	if (maildir_read_letter_close(&rl) == -1)
		return -1;

	return 0;

	close:
	maildir_read_letter_close(&rl);
	return rv;
}

int
save_letter(int cur, struct letter *letter, struct ignore *ignore, 
	struct reorder *reorder, int linewrap)
{
	char path[] = PATH_TMPDIR "/save.XXXXXX";
	FILE *fp;
	int fd;

	if ((fd = mkostemp(path, O_CLOEXEC)) == -1)
		return -1;
	if ((fp = fdopen(fd, "w")) == NULL) {
		close(fd);
		goto unlink;
	}

	if (read_letter_quick(cur, letter, ignore, reorder, fp, 0, 0) != 0)
		goto fp;

	if (fflush(fp) == EOF)
		goto fp;

	printf("letter located at %s\n", path);

	fclose(fp);
	return 0;

	fp:
	fclose(fp);
	unlink:
	unlink(path);
	return -1;
}
