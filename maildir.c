#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "address.h"
#include "argv.h"
#include "getline.h"
#include "letter.h"
#include "maildir.h"
#include "maildir-read.h"
#include "maildir-read-letter.h"
#include "pathnames.h"
#include "utf8.h"

#define MAILDIR_LETTER_READ_ERR -2
#define MAILDIR_LETTER_READ_EOF -1

static int letter_date_cmp(const void *, const void *);
static int maildir_letter_read(FILE *, struct getline *, struct letter *);

struct maildir_read_letter
maildir_read_letter(const char *root, const char *letter, int dev_null,
	FILE *out, int retain, struct argv_shm *ignore, struct argv_shm *reorder)
{
	char *argv[6], path[PATH_MAX], i[20], r[20];
	struct maildir_read_letter rv;
	struct utf8_decode u8;
	FILE *fp;
	pid_t pid;
	ssize_t nr;
	int argc, n, pe[2], po[2], status;

	n = snprintf(path, sizeof(path), "%s/cur/%s", root, letter);
	if (n < 0) {
		rv.save_errno = errno;
		rv.status = MAILDIR_READ_LETTER_SNPRINTF;
		return rv;
	}
	if (n >= sizeof(path)) {
		rv.save_errno = errno;
		rv.status = MAILDIR_READ_LETTER_TOOLONG;
		return rv;
	}

	if (ignore->fd != -1) {
		if (retain)
			n = snprintf(i, sizeof(i), "-u%lld", ignore->sz);
		else
			n = snprintf(i, sizeof(i), "-i%lld", ignore->sz);
		if (n < 0 || n >= sizeof(i)) {
			rv.save_errno = errno;
			rv.status = MAILDIR_READ_LETTER_SNPRINTF;
			return rv;
		}
	}
	if (reorder->fd != -1) {
		n = snprintf(r, sizeof(r), "-r%lld", reorder->sz);
		if (n < 0 || n >= sizeof(r)) {
			rv.save_errno = errno;
			rv.status = MAILDIR_READ_LETTER_SNPRINTF;
			return rv;
		}
	}

	if (pipe(pe) == -1) {
		rv.save_errno = errno;
		rv.status = MAILDIR_READ_LETTER_PIPE;
		return rv;
	}
	if (pipe(po) == -1) {
		rv.save_errno = errno;
		rv.status = MAILDIR_READ_LETTER_PIPE;
		return rv;
	}

	argc = 0;
	argv[argc++] = "maildir-read-letter";
	if (ignore->fd != -1) {
		argv[argc++] = i;
	}
	if (reorder->fd != -1) {
		argv[argc++] = r;
	}
	argv[argc++] = "--";
	argv[argc++] = path;
	argv[argc++] = NULL;

	switch (pid = fork()) {
	case -1:
		rv.save_errno = errno;
		rv.status = MAILDIR_READ_LETTER_FORK;
		return rv;
	case 0:
		if (dup2(dev_null, STDIN_FILENO) == -1)
			exit(MAILDIR_READ_LETTER_DUP);
		if (dup2(po[1], STDOUT_FILENO) == -1)
			exit(MAILDIR_READ_LETTER_DUP);
		if (dup2(pe[1], STDERR_FILENO) == -1)
			exit(MAILDIR_READ_LETTER_DUP);
		if (ignore->fd != -1 && dup2(ignore->fd, 3) == -1)
			exit(MAILDIR_READ_LETTER_DUP);
		if (reorder->fd != -1 && dup2(reorder->fd, 4) == -1)
			exit(MAILDIR_READ_LETTER_DUP);
		execv(PATH_MAILDIR_READ_LETTER, argv);
		exit(MAILDIR_READ_LETTER_EXEC);
	default:
		break;
	}

	if (close(po[1]) == -1 || close(pe[1]) == -1) {
		rv.save_errno = errno;
		rv.status = MAILDIR_READ_LETTER_CLOSE;
		return rv;
	}

	if ((fp = fdopen(po[0], "r")) == NULL) {
		rv.save_errno = errno;
		rv.status = MAILDIR_READ_LETTER_FDOPEN;
		return rv;
	}

	memset(&u8, 0, sizeof(u8));
	for (;;) {
		int c;

		if ((c = fgetc(fp)) == EOF) {
			if (u8.n != 0) {
				rv.save_errno = errno;
				rv.status = MAILDIR_READ_LETTER_UTF8;
				return rv;
			}
			break;
		}

		/*
		 * If this decoding fails then there is a bug within 
		 * maildir-read-letter
		 */
		switch (utf8_decode(&u8, c)) {
		case UTF8_DECODE_DONE:
			if (u8.n == 1
					&& !isprint((unsigned char)u8.buf[0])
					&& !isspace((unsigned char)u8.buf[0])) {
				rv.save_errno = errno;
				rv.status = MAILDIR_READ_LETTER_ASCII;
				return rv;
			}
			if (fwrite(u8.buf, u8.n, 1, out) != 1) {
				rv.save_errno = errno;
				rv.status = MAILDIR_READ_LETTER_PRINTF;
				return rv;
			}
			u8.n = 0;
			break;
		case UTF8_DECODE_INVALID:
			rv.save_errno = errno;
			rv.status = MAILDIR_READ_LETTER_ASCII;
			return rv;
		case UTF8_DECODE_MORE:
			continue;
		}
	}

	if (waitpid(pid, &status, 0) == -1) {
		rv.save_errno = errno;
		rv.status = MAILDIR_READ_LETTER_WAITPID;
		return rv;
	}

	nr = read(pe[0], &rv.save_errno, sizeof(rv.save_errno));
	if (nr == -1) {
		rv.save_errno = errno;
		rv.status = MAILDIR_READ_LETTER_READ;
		return rv;
	}
	else if (nr != sizeof(rv.save_errno)) {
		rv.save_errno = errno;
		rv.status = MAILDIR_READ_LETTER_SREAD;
		return rv;
	}
	else
		rv.status = WEXITSTATUS(status);

	fclose(fp);
	close(pe[0]);
	return rv;
}

int
maildir_setup(const char *path, int dev_null)
{
	pid_t pid;
	ssize_t nr;
	int p[2], pe[2], status;
	FILE *e;

	if (pipe(p) == -1) {
		warn("pipe");
		return -1;
	}

	switch (pid = fork()) {
	case -1:
		warn("fork");
		(void) close(p[0]);
		(void) close(p[1]);
		return -1;
	case 0:
		/* do this first so err sends to our stderr pipe */
		if (dup2(p[1], STDERR_FILENO) == -1)
			err(1, "dup2");
		if (dup2(dev_null, STDIN_FILENO) == -1)
			err(1, "dup2");
		if (dup2(dev_null, STDOUT_FILENO) == -1)
			err(1, "dup2");
		if (close(p[0]) == -1)
			err(1, "close");
		if (close(p[1]) == -1)
			err(1, "close");
		execl(PATH_MAILDIR_SETUP, "maildir-setup", path, NULL);
		err(1, "%s", PATH_MAILDIR_SETUP);
	default:
		break;
	}

	if (close(p[1]) == -1) {
		warn("close");
		(void) kill(pid, SIGKILL);
		(void) waitpid(pid, NULL, 0);
		return -1;
	}

	if (waitpid(pid, &status, 0) == -1) {
		warn("waitpid");
		(void) close(p[0]);
		(void) close(p[1]);
		return -1;
	}

	if (WEXITSTATUS(status) != 0) {
		struct getline gl;
		ssize_t len;
		FILE *e;
		int eo;

		if ((e = fdopen(p[0], "r")) == NULL) {
			warn("fdopen");
			(void) close(p[0]);
			return -1;
		}

		memset(&gl, 0, sizeof(gl));

		if ((len = getdelim(&gl.line, &gl.n, EOF, e)) == -1) {
			warn("getdelim");
			(void) fclose(e);
			return -1;
		}

		(void) fclose(e);

		if (mbstowcs(NULL, gl.line, len) < 0) {
			warnx("maildir-setup gave invalid error message");
			return -1;
		}
		if (fputs(gl.line, stderr) == EOF) {
			warn("fputs");
			return -1;
		}
		return -1;
	}

	fail:
	if (close(p[0]) == -1) {
		warn("close");
		return -1;
	}
	return 0;
}

struct maildir_read
maildir_read(char *path, int dev_null, int view_all)
{
	char *argv[5];
	struct letter *letters;
	struct getline gl;
	FILE *fp;
	size_t nletters;
	ssize_t nr;
	pid_t pid;
	uint8_t need_recache;
	struct maildir_read rv;
	int argc, po[2], pe[2], status;

	if (pipe(po) == -1) {
		rv.val.save_errno = errno;
		rv.status = MAILDIR_READ_PIPE;
		return rv;
	}
	if ((fp = fdopen(po[0], "r")) == NULL) {
		rv.val.save_errno = errno;
		rv.status = MAILDIR_READ_PIPE;
		(void) close(po[0]);
		(void) close(po[1]);
		return rv;
	}

	if (pipe(pe) == -1) {
		rv.val.save_errno = errno;
		rv.status = MAILDIR_READ_PIPE;
		(void) fclose(fp);
		(void) close(po[1]);
		return rv;
	}

	argc = 0;
	argv[argc++] = "maildir-read";
	if (view_all)
		argv[argc++] = "-a";
	argv[argc++] = "--";
	argv[argc++] = path;
	argv[argc++] = NULL;

	switch (pid = fork()) {
	case -1:
		rv.val.save_errno = errno;
		rv.status = MAILDIR_READ_FORK;
		return rv;
	case 0:
		if (dup2(dev_null, STDIN_FILENO) == -1)
			exit(MAILDIR_READ_DUP);
		if (dup2(po[1], STDOUT_FILENO) == -1)
			exit(MAILDIR_READ_DUP);
		if (dup2(pe[1], STDERR_FILENO) == -1)
			exit(MAILDIR_READ_DUP);
		execv(PATH_MAILDIR_READ, argv);
		exit(MAILDIR_READ_EXEC);
	default:
		break;
	}
	if (close(po[1]) == -1 || close(pe[1]) == -1) {
		rv.val.save_errno = errno;
		rv.status = MAILDIR_READ_CLOSE;
		return rv;
	}

	if (fread(&need_recache, sizeof(need_recache), 1, fp) != 1) {
		rv.val.save_errno = errno;
		rv.status = MAILDIR_READ_READ;
		(void) fclose(fp);
		(void) close(pe[0]);
		return rv;
	}

	nletters = 0;
	letters = NULL;
	memset(&gl, 0, sizeof(gl));
	for (;;) {
		struct letter letter, *t;

		switch (maildir_letter_read(fp, &gl, &letter)) {
		case MAILDIR_LETTER_READ_ERR:
			goto fail;
		case MAILDIR_LETTER_READ_EOF:
			goto done;
		default:
			break;
		}

		t = reallocarray(letters, nletters + 1, sizeof(*letters));
		if (t == NULL) {
			free(letter.from.str);
			free(letter.path);
			free(letter.subject);
			goto fail;
		}
		letters = t;
		letters[nletters++] = letter;
	}
	done:

	if (waitpid(pid, &status, 0) == -1) {
		rv.val.save_errno = errno;
		rv.status = MAILDIR_READ_WAITPID;
		goto fail;
	}

	nr = read(pe[0], &rv.val.save_errno, sizeof(rv.val.save_errno));
	if (nr == -1) {
		rv.val.save_errno = errno;
		rv.status = MAILDIR_READ_READ;
		goto fail;
	}
	else if (nr != sizeof(rv.val.save_errno)) {
		rv.val.save_errno = 0;
		rv.status = MAILDIR_READ_SREAD;
		goto fail;
	}
	else
		rv.status = MAILDIR_READ_OK;

	if (close(pe[0]) == -1) {
		rv.val.save_errno = 0;
		rv.status = MAILDIR_READ_CLOSE;
		goto fail;
	}
	if (fclose(fp) == -1) {
		rv.val.save_errno = 0;
		rv.status = MAILDIR_READ_CLOSE;
		goto fail;
	}

	qsort(letters, nletters, sizeof(*letters), letter_date_cmp);

	free(gl.line);
	rv.val.good.letters = letters;
	rv.val.good.nletters = nletters;
	rv.val.good.need_recache = need_recache;
	return rv;

	fail:
	for (size_t i = 0; i < nletters; i++) {
		free(letters[i].from.str);
		free(letters[i].path);
		free(letters[i].subject);
	}
	free(letters);
	free(gl.line);
	return rv;
}

static int
letter_date_cmp(const void *one, const void *two)
{
	const struct letter *n1 = one, *n2 = two;

	if (n1->date > n2->date)
		return 1;
	else if (n1->date == n2->date)
		return 0;
	else
		return -1;
}

static int
maildir_letter_read(FILE *fp, struct getline *gl, struct letter *out)
{
	char *f, *path, *subject;
	struct from_safe from;
	size_t n;
	ssize_t len;
	time_t date;

	n = fread(&date, 1, sizeof(date), fp);
	if (n == 0)
		return MAILDIR_LETTER_READ_EOF;
	else if (n != sizeof(date))
		return MAILDIR_LETTER_READ_ERR;
	if (localtime(&date) == NULL)
		return MAILDIR_LETTER_READ_ERR;

	if ((len = getdelim(&gl->line, &gl->n, '\0', fp)) == -1)
		return MAILDIR_LETTER_READ_ERR;
	/* not >= because len includes the NUL terminator */
	if (len > NAME_MAX)
		return MAILDIR_LETTER_READ_ERR;
	if ((path = strdup(gl->line)) == NULL)
		return MAILDIR_LETTER_READ_ERR;

	if (getdelim(&gl->line, &gl->n, '\0', fp) == -1)
		goto path;
	if (mbstowcs(NULL, gl->line, 0) == (size_t) -1)
		goto path;
	if ((f = strdup(gl->line)) == NULL)
		goto path;
	if (from_safe_new(f, &from) == -1) {
		free(f);
		goto path;
	}

	if ((len = getdelim(&gl->line, &gl->n, '\0', fp)) == -1)
		goto from;
	if (mbstowcs(NULL, gl->line, 0) == (size_t) -1)
		goto path;
	if (len == 0)
		subject = NULL;
	else {
		if ((subject = strdup(gl->line)) == NULL)
			goto from;
	}

	out->date = date;
	out->from = from;
	out->path = path;
	out->subject = subject;
	return 0;

	from:
	free(from.str);
	path:
	free(path);
	return MAILDIR_LETTER_READ_ERR;
}
