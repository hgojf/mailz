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
#include "edit.h"
#include "config.h"
#include "getline.h"
#include "letter.h"
#include "maildir.h"
#include "pathnames.h"
#include "utf8.h"

#define MAILDIR_LETTER_READ_ERR -2
#define MAILDIR_LETTER_READ_EOF -1

static int letter_date_cmp(const void *, const void *);
static int maildir_letter_read(FILE *, struct getline *, struct letter *);
static void output_childerr(int);

int
maildir_read_letter(const char *root, const char *letter, int dev_null,
	FILE *out, struct ignore *ignore, struct reorder *reorder)
{
	char *argv[6], path[PATH_MAX], i[20], r[20];
	struct utf8_decode u8;
	FILE *fp;
	pid_t pid;
	int argc, n, pe[2], po[2], status;

	n = snprintf(path, sizeof(path), "%s/cur/%s", root, letter);
	if (n < 0) {
		warn("snprintf");
		return -1;
	}
	if (n >= sizeof(path)) {
		warnc(ENAMETOOLONG, "%s/cur/%s", root, letter);
		return -1;
	}

	switch (ignore->type) {
	case IGNORE_ALL:
		(void)strlcpy(i, "-a", sizeof(i));
		break;
	case IGNORE_NONE:
		break;
	case IGNORE_IGNORE:
		n = snprintf(i, sizeof(i), "-i%lld", ignore->shm.sz);
		if (n < 0 || n >= sizeof(i)) {
			warn("snprintf");
			return -1;
		}
		break;
	case IGNORE_RETAIN:
		n = snprintf(i, sizeof(i), "-u%lld", ignore->shm.sz);
		if (n < 0 || n >= sizeof(i)) {
			warn("snprintf");
			return -1;
		}
		break;
	}
	if (reorder->shm.fd != -1) {
		n = snprintf(r, sizeof(r), "-r%lld", reorder->shm.sz);
		if (n < 0 || n >= sizeof(r)) {
			warn("snprintf");
			return -1;
		}
	}

	if (pipe(pe) == -1) {
		warn("pipe");
		return -1;
	}
	if (pipe(po) == -1) {
		warn("pipe");
		(void) close(pe[1]);
		(void) close(pe[0]);
		return -1;
	}

	argc = 0;
	argv[argc++] = "maildir-read-letter";
	if (ignore->type != IGNORE_NONE)
		argv[argc++] = i;
	if (reorder->shm.fd != -1)
		argv[argc++] = r;
	argv[argc++] = "--";
	argv[argc++] = path;
	argv[argc++] = NULL;

	switch (pid = fork()) {
	case -1:
		warn("fork");
		(void) close(pe[1]);
		(void) close(pe[0]);
		(void) close(po[1]);
		(void) close(po[0]);
		return -1;
	case 0:
		if (dup2(pe[1], STDERR_FILENO) == -1)
			err(1, "dup2");
		if (dup2(dev_null, STDIN_FILENO) == -1)
			err(1, "dup2");
		if (dup2(po[1], STDOUT_FILENO) == -1)
			err(1, "dup2");
		if (ignore->type == IGNORE_IGNORE || ignore->type == IGNORE_RETAIN) {
			if (dup2(ignore->shm.fd, 3) == -1)
				err(1, "dup2");
		}
		if (reorder->shm.fd != -1 && dup2(reorder->shm.fd, 4) == -1)
			err(1, "dup2");
		execv(PATH_MAILDIR_READ_LETTER, argv);
		err(1, "%s", PATH_MAILDIR_READ_LETTER);
	default:
		break;
	}

	(void) close(po[1]);
	(void) close(pe[1]);

	if ((fp = fdopen(po[0], "r")) == NULL) {
		warn("fdopen");
		(void) close(po[0]);
		(void) close(pe[0]);
		(void) kill(pid, SIGKILL);
		(void) waitpid(pid, NULL, 0);
		return -1;
	}

	memset(&u8, 0, sizeof(u8));
	for (;;) {
		int c;

		if ((c = fgetc(fp)) == EOF) {
			if (u8.n != 0) {
				warnx("invalid utf8");
				goto fail;
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
				warn("invalid ascii");
				goto fail;
			}
			if (fwrite(u8.buf, u8.n, 1, out) != 1) {
				if (errno == EPIPE) {
					(void) kill(pid, SIGKILL);
					(void) waitpid(pid, NULL, 0);
					(void) fclose(fp);
					(void) close(pe[0]);
					return 0;
				}
				warn("write");
				goto fail;
			}
			u8.n = 0;
			break;
		case UTF8_DECODE_INVALID:
			warnx("invalid ascii");
			goto fail;
		case UTF8_DECODE_MORE:
			continue;
		}
	}

	if (waitpid(pid, &status, 0) == -1) {
		warn("waitpid");
		return -1;
	}

	if (WEXITSTATUS(status) != 0) {
		output_childerr(pe[0]);
		return -1;
	}

	fclose(fp);
	close(pe[0]);
	return 0;

	fail:
	(void) kill(pid, SIGKILL);
	(void) waitpid(pid, NULL, 0);
	(void) fclose(fp);
	(void) close(pe[0]);
	return -1;
}

int
maildir_setup(const char *path, int dev_null)
{
	pid_t pid;
	int p[2], status;

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
		output_childerr(p[0]);
		return -1;
	}

	if (close(p[0]) == -1) {
		warn("close");
		return -1;
	}
	return 0;
}

int
maildir_read(char *path, int dev_null, int view_all, struct maildir_read *out)
{
	char *argv[5];
	struct letter *letters;
	struct getline gl;
	FILE *fp;
	size_t nletters;
	pid_t pid;
	uint8_t need_recache;
	int argc, po[2], pe[2], status;

	if (pipe(po) == -1) {
		warn("pipe");
		return -1;
	}
	if ((fp = fdopen(po[0], "r")) == NULL) {
		warn("fdopen");
		(void) close(po[0]);
		(void) close(po[1]);
		return -1;
	}

	if (pipe(pe) == -1) {
		warn("pipe");
		(void) fclose(fp);
		(void) close(po[1]);
		return -1;
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
		warn("fork");
		(void) close(po[1]);
		(void) fclose(fp);
		(void) close(pe[1]);
		(void) close(pe[0]);
		return -1;
	case 0:
		if (dup2(pe[1], STDERR_FILENO) == -1)
			err(1, "dup2");
		if (dup2(dev_null, STDIN_FILENO) == -1)
			err(1, "dup2");
		if (dup2(po[1], STDOUT_FILENO) == -1)
			err(1, "dup2");
		execv(PATH_MAILDIR_READ, argv);
		err(1, "%s", PATH_MAILDIR_READ);
	default:
		break;
	}

	(void) close(po[1]);
	(void) close(pe[1]);

	if (fread(&need_recache, sizeof(need_recache), 1, fp) != 1) {
		warn("fread");
		(void) fclose(fp);
		(void) close(pe[0]);
		return -1;
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
		warn("waitpid");
		goto fail;
	}

	(void) fclose(fp);
	free(gl.line);

	if (WEXITSTATUS(status) != 0) {
		output_childerr(pe[0]);
		return -1;
	}

	(void) close(pe[0]);

	qsort(letters, nletters, sizeof(*letters), letter_date_cmp);

	out->letters = letters;
	out->nletters = nletters;
	out->need_recache = need_recache;
	return 0;

	fail:
	for (size_t i = 0; i < nletters; i++) {
		free(letters[i].from.str);
		free(letters[i].path);
		free(letters[i].subject);
	}
	free(letters);
	free(gl.line);
	return -1;
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

static void
output_childerr(int serr)
{
	FILE *e;
	ssize_t len;
	struct getline gl;

	if ((e = fdopen(serr, "r")) == NULL) {
		warn("fdopen");
		return;
	}

	memset(&gl, 0, sizeof(gl));
	if ((len = getdelim(&gl.line, &gl.n, EOF, e)) == -1) {
		warn("getdelim");
		(void) fclose(e);
		free(gl.line);
		return;
	}

	(void) fclose(e);
	if (mbstowcs(NULL, gl.line, len) == (size_t) -1) {
		warnx("child sent invalid error message");
		free(gl.line);
		return;
	}
	if (fputs(gl.line, stderr) == EOF) {
		warn("fputs");
		free(gl.line);
	}
}
