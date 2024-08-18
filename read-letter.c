#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "conf.h"
#include "extern.h"
#include "imsg-util.h"
#include "maildir-read-letter.h"
#include "pathnames.h"
#include "read-letter.h"

static int imsg_compose_argv(struct imsgbuf *, uint32_t, char **, size_t);

int
read_letter(int cur, const char *path, struct ignore *ignore, 
	struct reorder *reorder, long long linewrap, struct read_letter *out)
{
	struct imsgbuf msgbuf;
	FILE *o;
	pid_t pid;
	int p[2], sv[2];

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
			PF_UNSPEC, sv) == -1)
		return -1;
	if (pipe2(p, O_CLOEXEC) == -1)
		goto sv;
	if ((o = fdopen(p[0], "r")) == NULL) {
		close(p[0]);
		close(p[1]);
		goto sv;
	}

	switch (pid = fork()) {
	case -1:
		goto p;
	case 0: {
		int fd;

		if ((fd = openat(cur, path, O_RDONLY | O_CLOEXEC)) == -1)
			err(1, "%s", path);
		if (dup2(fd, STDIN_FILENO) == -1)
			err(1, "dup2");
		if (dup2(p[1], STDOUT_FILENO) == -1)
			err(1, "dup2");
		if (dup2(dev_null, STDERR_FILENO) == -1)
			err(1, "dup2");
		if (dup2(sv[1], 3) == -1)
			err(1, "dup2");
		execl(PATH_MAILDIR_READ_LETTER, "maildir-read-letter", NULL);
		err(1, "%s", PATH_MAILDIR_READ_LETTER);
	}
	default:
		break;
	}

	shutdown(sv[1], SHUT_RDWR); /* avoid loopback */

	imsg_init(&msgbuf, sv[0]);

	switch (ignore->type) {
	case IGNORE_IGNORE:
	case IGNORE_RETAIN: {
		uint32_t type;

		if (ignore->argc == 0)
			break;
		switch (ignore->type) {
		case IGNORE_IGNORE:
			type = IMSG_MDR_IGNORE;
			break;
		case IGNORE_RETAIN:
			type = IMSG_MDR_RETAIN;
			break;
			break;
		}
		if (imsg_compose_argv(&msgbuf, type, ignore->argv, 
				ignore->argc) == -1)
			goto kill;
		break;
	}
	case IGNORE_ALL:
		if (imsg_compose(&msgbuf, IMSG_MDR_IGNOREALL, 0, -1, -1, 
				NULL, 0) == -1)
			goto kill;
		break;
	}

	if (reorder->argc != 0) {
		if (imsg_compose_argv(&msgbuf, IMSG_MDR_REORDER, reorder->argv, 
				reorder->argc) == -1)
			goto kill;
	}

	if (linewrap != 0) {
		if (imsg_compose(&msgbuf, IMSG_MDR_LINEWRAP, 0, -1, -1, &linewrap, sizeof(linewrap)) == -1)
			goto kill;
	}

	if (imsg_flush_blocking(&msgbuf) == -1)
		goto kill;

	close(p[1]);

	close(sv[0]);
	close(sv[1]);
	imsg_clear(&msgbuf);

	out->o = o;
	out->pid = pid;
	memset(&out->mbs, 0, sizeof(out->mbs));

	return 0;

	kill:
	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, NULL, 0);
	imsg_clear(&msgbuf);
	p:
	fclose(o);
	close(p[1]);
	sv:
	close(sv[0]);
	close(sv[1]);
	return -1;
}

int
read_letter_close(struct read_letter *letter)
{
	int status;

	fclose(letter->o);

	if (waitpid(letter->pid, &status, 0) == -1)
		return -1;
	if (WEXITSTATUS(status) != 0)
		return -1;
	return 0;
}

int
read_letter_getc(struct read_letter *letter, char buf[static 4])
{
	for (int i = 0; i < 4; i++) {
		int c;
		char cc;

		if ((c = fgetc(letter->o)) == EOF) {
			if (i == 0)
				return 0;
			/* EOF in the middle of decoding */
			errno = EILSEQ;
			return -1;
		}

		cc = c;

		switch (mbrtowc(NULL, &cc, 1, &letter->mbs)) {
		case -3: /* not sure how to handle this */
			errno = EILSEQ;
		case -1:
			return -1;
		case -2:
			buf[i] = cc;
			continue;
		case 0:
			errno = EILSEQ;
			return -1;
		default:
			buf[i] = cc;
			if (i == 0) {
				if (!isascii(c) || (!isprint(c) && !isspace(c))) {
					errno = EILSEQ;
					return -1;
				}
			}
			return i + 1;
		}
	}

	/* mbrtowc should return -1 before this */

	errno = EILSEQ;
	return -1;
}

int
read_letter_quick(int cur, const char *path, struct ignore *ignore,
	struct reorder *reorder, long long linewrap, FILE *out)
{
	struct read_letter rl;
	int rv;

	rv = -1;

	if (read_letter(cur, path, ignore, reorder, linewrap, &rl) == -1)
		return -1;

	for (;;) {
		char buf[4];
		int n;

		if ((n = read_letter_getc(&rl, buf)) == -1)
			goto close;
		if (n == 0)
			break;

		if (fwrite(buf, n, 1, out) != 1) {
			if (ferror(out) && errno == EPIPE)
				rv = 0;
			goto close;
		}
	}

	rv = 0;
	close:
	if (read_letter_close(&rl) == -1)
		return -1;
	return rv;
}

static int
imsg_compose_argv(struct imsgbuf *msgbuf, uint32_t type, char **argv, 
	size_t argc)
{
	struct ibuf *ibuf;

	if ((ibuf = ibuf_dynamic(0, (size_t)-1)) == NULL)
		return -1;

	for (size_t i = 0; i < argc; i++) {
		if (ibuf_add(ibuf, argv[i], strlen(argv[i]) + 1) == -1) {
			ibuf_free(ibuf);
			return -1;
		}
	}

	return imsg_compose_ibuf(msgbuf, type, 0, -1, ibuf);
}
