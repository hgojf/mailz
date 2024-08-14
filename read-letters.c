#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "extern.h"
#include "imsg-util.h"
#include "letter.h"
#include "cache.h"
#include "maildir-read.h"
#include "pathnames.h"
#include "printable.h"
#include "read-letters.h"

static int imsg_get_letter(struct imsg *, const char *, struct letter *);
static int letter_date_cmp(const void *, const void *);

int
maildir_read(int curfd, int do_cache, int view_all, 
	struct letter **out_letters, size_t *out_nletter)
{
	struct cache cache;
	struct imsgbuf msgbuf;
	struct letter *letters;
	DIR *cur;
	size_t nletter;
	int cur2, rv, status, sv[2];
	pid_t pid;

	rv = -1;

	if (do_cache) {
		struct stat sb1, sb2;
		int cache_fd;
		FILE *fp;

		if ((cache_fd = openat(curfd, "../.mailzcache", O_RDONLY | O_CLOEXEC)) == -1) {
			if (errno == ENOENT) {
				memset(&cache, 0, sizeof(cache));
				goto nocache;
			}
			return -1;
		}

		if ((fp = fdopen(cache_fd, "r")) == NULL) {
			close(cache_fd);
			return -1;
		}

		if (fstat(curfd, &sb1) == -1 || fstat(cache_fd, &sb2) == -1) {
			fclose(fp);
			return -1;
		}

		switch (cache_read(fp, &cache)) {
		case CACHE_VERSION_MISMATCH:
			memset(&cache, 0, sizeof(cache));
			fclose(fp);
			goto nocache;
		case -1:
			fclose(fp);
			return -1;
		default:
			break;
		}

		/* cache is up to date */
		if (timespeccmp(&sb1.st_mtim, &sb2.st_mtim, <=) && (view_all == cache.view_all)) {
			qsort(cache.entries, cache.nentry, sizeof(*cache.entries), letter_date_cmp);
			*out_letters = cache.entries;
			*out_nletter = cache.nentry;
			fclose(fp);
			return 0;
		}

		/* cache is not up to date, but can still be of use */
		fclose(fp);
	}
	else
		memset(&cache, 0, sizeof(cache));
	nocache:

	if ((cur2 = dup(curfd)) == -1)
		goto cache;
	if ((cur = fdopendir(cur2)) == NULL) {
		close(cur2);
		goto cache;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
			PF_UNSPEC, sv) == -1)
		return -1;
	switch (pid = fork()) {
	case -1:
		close(sv[0]);
		close(sv[1]);
		goto cur;
	case 0:
		for (int i = 0; i <= STDERR_FILENO; i++)
			if (dup2(dev_null, i) == -1)
				err(1, "dup2");
		if (dup2(sv[1], STDERR_FILENO + 1) == -1)
			err(1, "dup2");
		execl(PATH_MAILDIR_READ, "maildir-read", NULL);
		err(1, "%s", PATH_MAILDIR_READ);
	default:
		break;
	}
	close(sv[1]);

	imsg_init(&msgbuf, sv[0]);

	letters = NULL;
	nletter = 0;
	for (;;) {
		struct letter letter, *t;
		struct imsg msg;
		struct dirent *de;
		int fd;

		errno = 0;
		if ((de = readdir(cur)) == NULL) {
			if (errno == 0)
				break;
			goto pid;
		}

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;

		if (!view_all && letter_seen(de->d_name))
			continue;

		if (cache_take(&cache, de->d_name, &letter) == 0) {
			t = reallocarray(letters, nletter + 1, sizeof(*letters));
			if (t == NULL) {
				letter_free(&letter);
				goto pid;
			}
			letters = t;
			letters[nletter++] = letter;

			continue;
		}

		if ((fd = openat(cur2, de->d_name, O_RDONLY)) == -1)
			goto pid;

		if (imsg_compose(&msgbuf, IMSG_MDR_FILE, 0, -1, fd, NULL, 0) == -1) {
			close(fd);
			goto pid;
		}

		if (imsg_flush_blocking(&msgbuf) == -1)
			goto pid;

		switch (imsg_get_blocking(&msgbuf, &msg)) {
		case -1:
		case 0:
			goto pid;
		default:
			break;
		}

		if (imsg_get_type(&msg) != IMSG_MDR_LETTER 
				|| imsg_get_letter(&msg, de->d_name, &letter) == -1) {
			imsg_free(&msg);
			goto pid;
		}

		t = reallocarray(letters, nletter + 1, sizeof(*letters));
		if (t == NULL) {
			imsg_free(&msg);
			letter_free(&letter);
			goto pid;
		}
		letters = t;
		letters[nletter++] = letter;
	}

	shutdown(msgbuf.fd, SHUT_RDWR);
	if (waitpid(pid, &status, 0) == -1)
		goto msgbuf;
	if (WEXITSTATUS(status) != 0)
		goto msgbuf;

	qsort(letters, nletter, sizeof(*letters), letter_date_cmp);
	*out_letters = letters;
	*out_nletter = nletter;
	rv = 0;
	pid:
	if (rv == -1) {
		(void)kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		for (size_t i = 0; i < nletter; i++)
			letter_free(&letters[i]);
		free(letters);
	}
	msgbuf:
	close(msgbuf.fd);
	imsg_clear(&msgbuf);
	cur:
	closedir(cur);
	cache:
	cache_free(&cache);
	return rv;
}

static int
ibuf_get_string_printable(struct ibuf *ibuf, char **out)
{
	int rv;

	switch (rv = ibuf_get_string(ibuf, out)) {
	case -1:
	case -2:
		return rv;
	default:
		if (!string_isprint(*out)) {
			errno = EILSEQ;
			free(*out);
			return -1;
		}
		return rv;
	}
}

static int
imsg_get_letter(struct imsg *msg, const char *path, struct letter *out)
{
	struct ibuf ibuf;

	if (imsg_get_type(msg) != IMSG_MDR_LETTER) {
		errno = EBADMSG;
		return -1;
	}
	if (imsg_get_ibuf(msg, &ibuf) == -1)
		return -1;

	if (ibuf_get(&ibuf, &out->date, sizeof(out->date)) == -1) {
		errno = EBADMSG;
		return -1;
	}

	if (localtime(&out->date) == NULL) {
		errno = EBADMSG;
		return -1;
	}

	switch (ibuf_get_string_printable(&ibuf, &out->from.addr)) {
	case -2:
		errno = EBADMSG;
	case -1:
		return -1;
	default:
		break;
	}

	if (ibuf_get_string_printable(&ibuf, &out->from.name) == -1)
		goto addr;

	if (ibuf_get_string_printable(&ibuf, &out->message_id) == -1)
		goto name;

	if (ibuf_get_string_printable(&ibuf, &out->subject) == -1)
		goto message_id;

	if (ibuf_size(&ibuf) != 0) {
		errno = EBADMSG;
		goto subject;
	}

	if ((out->path = strdup(path)) == NULL)
		goto subject;

	return 0;

	subject:
	free(out->subject);
	message_id:
	free(out->message_id);
	name:
	free(out->from.name);
	addr:
	free(out->from.addr);
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
	return -1;
}
