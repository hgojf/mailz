#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "pathnames.h"
#include "sendmail.h"

int
sendmail(struct sendmail *letter)
{
	FILE *fp;
	pid_t pid;
	char date[32], path[] = "/tmp/mail/send.XXXXXXXXXX";
	struct tm *tm;
	time_t cur;
	int fd;

	assert(letter->from.addr != NULL);
	assert(letter->from.name != NULL);
	assert(letter->to != NULL);
	/* assert(letter->subject != NULL); */

	if ((cur = time(NULL)) == -1) {
		/* cant happen w/64 bit time... */
		warn("time");
		return -1;
	}

	if ((tm = localtime(&cur)) == NULL) {
		warnx("localtime");
		return -1;
	}

	if (strftime(date, 32, "%a, %d %b %Y %H:%M:%S %z", tm) == 0) {
		warnx("strftime");
		return -1;
	}

	if ((fd = mkstemp(path)) == -1) {
		warn("mkstemp");
		return -1;
	}
	if ((fp = fdopen(fd, "w")) == NULL) {
		warn("fdopen");
		close(fd);
		return -1;
	}

	if (fprintf(fp, "From: %s <%s>\n", letter->from.name, letter->from.addr) < 0) {
		warn("fprintf");
		goto fp;
	}
	if (fprintf(fp, "To: %s\n", letter->to) < 0) {
		warn("fprintf");
		goto fp;
	}
	if (letter->subject != NULL 
			&& fprintf(fp, "Subject: %s%s\n", letter->re ? "Re: " : "", letter->subject) < 0) {
		warn("fprintf");
		goto fp;
	}
	if (fprintf(fp, "Date: %s\n", date) < 0) {
		warn("fprintf");
		goto fp;
	}
	if (fputc('\n', fp) == EOF) {
		warn("fputc");
		goto fp;
	}
	if (fclose(fp) == EOF) {
		warn("fclose");
		return -1;
	}

	switch (pid = fork()) {
	case -1:
		warn("fork");
		return -1;
	case 0:
		execl(PATH_MAILZWRAPPER, "vi", path, NULL);
		err(1, "execl");
		/* NOTREACHED */
	default:
		waitpid(pid, NULL, 0);
		break;
	}

	switch (pid = fork()) {
	case -1:
		warn("fork");
		return -1;
	case 0:
		if ((fd = open(path, O_RDONLY)) == -1)
			err(1, "fopen");
		if (dup2(fd, STDIN_FILENO) == -1)
			err(1, "dup2");
		if (close(fd) == -1)
			err(1, "close");
		execl(PATH_MAILZWRAPPER, "sendmail", letter->to, NULL);
		err(1, "execl");
		/* NOTREACHED */
	default:
		waitpid(pid, NULL, 0);
	}

	return 0;

	fp:
	fclose(fp);
	return -1;
}
