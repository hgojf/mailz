#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "maildir-send.h"
#include "pathnames.h"

/*
 * sends an email with a seed that it will append to the body
 * of the message.
 * arguments are <from> <to> [subject]
 */
int
main(int argc, char *argv[])
{
	FILE *fp;
	const char *from, *subject, *to;
	ssize_t nw;
	pid_t pid;
	int p[2], rv, save_errno, status;

	if (argc < 3) {
		save_errno = 0;
		rv = MAILDIR_SEND_USAGE;
		goto fail;
	}

	from = argv[1];
	to = argv[2];
	subject = argv[3]; /* if argc == 3 this will be NULL */

	if (unveil(PATH_SENDMAIL, "x") == -1) {
		save_errno = errno;
		rv = MAILDIR_SEND_UNVEIL;
		goto fail;
	}
	if (pledge("stdio proc exec", NULL) == -1) {
		save_errno = errno;
		rv = MAILDIR_SEND_PLEDGE;
		goto fail;
	}

	if (pipe(p) == -1) {
		save_errno = errno;
		rv = MAILDIR_SEND_PIPE;
		goto fail;
	}

	signal(SIGPIPE, SIG_IGN);

	if ((fp = fdopen(p[1], "w")) == NULL) {
		save_errno = errno;
		rv = MAILDIR_SEND_FDOPEN;
		(void) close(p[1]);
		goto fail;
	}

	switch (pid = fork()) {
	case -1:
		save_errno = errno;
		rv = MAILDIR_SEND_FORK;
		(void) close(p[1]);
		(void) close(p[0]);
		goto fail;
	case 0:
		if (dup2(p[0], STDIN_FILENO) == -1)
			exit(MAILDIR_SEND_DUP);
		if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1)
			exit(MAILDIR_SEND_DUP);
		/* stderr is piped to /dev/null */
		if (close(p[0]) == -1)
			exit(MAILDIR_SEND_CLOSE);
		if (close(p[1]) == -1)
			exit(MAILDIR_SEND_CLOSE);
		execl(PATH_SENDMAIL, "sendmail", "-t", NULL);
		exit(MAILDIR_SEND_EXEC);
	default:
		break;
	}

	if (close(p[0]) == -1) {
		save_errno = errno;
		rv = MAILDIR_SEND_CLOSE;
		(void) close(p[1]);
		goto pid;
	}

	if (pledge("stdio", NULL) == -1) {
		save_errno = errno;
		rv = MAILDIR_SEND_PLEDGE;
		goto pid;
	}

	if (fprintf(fp, "From: %s\nTo: %s\n", from, to) < 0
			|| (subject != NULL && fprintf(fp, "Subject: %s\n", subject) < 0)
			|| fputc('\n', fp) == EOF) {
		save_errno = errno;
		rv = MAILDIR_SEND_PRINTF;
		goto pid;
	}

	for (;;) {
		int c;

		if ((c = fgetc(stdin)) == EOF) {
			if (ferror(stdin)) {
				save_errno = errno;
				rv = MAILDIR_SEND_GETC;
				goto pid;
			}
			break;
		}
		if (fputc(c, fp) == EOF) {
			save_errno = errno;
			rv = MAILDIR_SEND_PUTC;
			goto pid;
		}
	}

	if (fclose(fp) == EOF) {
		save_errno = errno;
		rv = MAILDIR_SEND_PUTC;
		goto pid;
	}
	fp = NULL;

	if (waitpid(pid, &status, 0) == -1) {
		save_errno = errno;
		rv = MAILDIR_SEND_WAITPID;
		goto fp;
	}

	if (status != 0) {
		save_errno = 0;
		rv = MAILDIR_SEND_SENDMAIL;
		goto fp;
	}

	save_errno = 0;
	rv = MAILDIR_SEND_OK;
	pid:
	if (rv != MAILDIR_SEND_OK) {
		(void) kill(pid, SIGKILL);
		(void) waitpid(pid, NULL, 0);
	}
	fp:
	if (fp != NULL && fclose(fp) == EOF && rv == MAILDIR_SEND_OK) {
		save_errno = errno;
		rv = MAILDIR_SEND_CLOSE;
	}
	fail:
	nw = write(STDOUT_FILENO, &save_errno, sizeof(save_errno));
	if (rv == MAILDIR_SEND_OK) {
		if (nw == -1) {
			rv = MAILDIR_SEND_WRITE;
		}
		else if (nw != sizeof(save_errno)) {
			rv = MAILDIR_SEND_SWRITE;
		}
	}
	return rv;
}
