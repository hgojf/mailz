#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "pathnames.h"
#include "send.h"

static int print_from(const char *, const struct sendmail_from *, FILE *);
static int print_header(const struct sendmail_header *, FILE *);

static int
date(time_t date, char buf[static 33])
{
	struct tm *tm;

	if ((tm = localtime(&date)) == NULL)
		return -1;

	if (strftime(buf, 33, "%a, %d %b %Y %H:%M:%S %z", tm) == 0)
		return -1;

	return 0;
}

int
sendmail_setup(const struct sendmail_subject *subject,
	const struct sendmail_from *from, 
	const struct sendmail_from *to, 
	const struct sendmail_header *headers, size_t nh,
	FILE *o)
{
	char db[33];

	if (date(time(NULL), db) == -1)
		return -1;
	if (fprintf(o, "Date: %s\n", db) < 0)
		return -1;

	if (print_from("From", from, o) == -1)
		return -1;
	if (print_from("To", to, o) == -1)
		return -1;

	if (subject->s != NULL) {
		if (fprintf(o, "Subject: %s%s\n", 
				subject->reply ? "Re: " : "", subject->s) < 0)
			return -1;
	}

	for (size_t i = 0; i < nh; i++)
		if (print_header(&headers[i], o) == -1)
			return -1;

	if (fputc('\n', o) == EOF)
		return -1;
	return 0;
}

int
sendmail_send(FILE *fp)
{
	pid_t pid;
	int fd, status;

	if (fseek(fp, 0, SEEK_SET) == -1)
		return -1;
	if ((fd = fileno(fp)) == -1)
		return -1;

	switch (pid = fork()) {
	case -1:
		return -1;
	case 0:
		if (dup2(fd, STDIN_FILENO) == -1)
			err(1, "dup2");
		execl(PATH_SENDMAIL, "sendmail", "-t", NULL);
		err(1, "%s", PATH_SENDMAIL);
	default:
		break;
	}

	if (waitpid(pid, &status, 0) == -1 || WEXITSTATUS(status) != 0)
		return -1;
	return 0;
}

static int
print_from(const char *h, const struct sendmail_from *from, FILE *o)
{
	int n;

	if (from->name != NULL)
		n = fprintf(o, "%s: %s <%s>\n", h, from->name, from->addr);
	else
		n = fprintf(o, "%s: %s\n", h, from->addr);

	if (n < 0)
		return -1;
	return 0;

}

static int
print_header(const struct sendmail_header *header, FILE *out)
{
	if (fprintf(out, "%s: %s\n", header->key, header->val) < 0)
		return -1;
	return 0;
}
