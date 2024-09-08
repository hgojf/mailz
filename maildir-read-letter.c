/*
 * Copyright (c) 2024 Henry Ford <fordhenry2299@gmail.com>

 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.

 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/queue.h>
#include <sys/tree.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <imsg.h>
#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "charset.h"
#include "content-type.h"
#include "encoding.h"
#include "header.h"
#include "ibuf-util.h"
#include "imsg-sync.h"
#include "maildir-read-letter.h"
#include "string-util.h"

struct header_rb {
	char *key;
	char **vals;
	size_t nval;
	RB_ENTRY(header_rb) entries;
};

RB_HEAD(headers, header_rb);

struct ignore {
	enum {
		IGNORE_IGNORE,
		IGNORE_RETAIN,
		IGNORE_ALL,
	} type;
	size_t argc;
	char **argv;
};

struct reorder {
	size_t argc;
	char **argv;
};

#define PARENT_FD 3

static void headers_free(struct headers *);
static void header_free(struct header_rb *);
static int header_ignore(struct ignore *, const char *);
static int header_print(struct header_rb *, FILE *);
static int header_rb_cmp(struct header_rb *, struct header_rb *);
RB_PROTOTYPE_STATIC(headers, header_rb, entries, header_rb_cmp);
static int imsg_get_argv(struct imsg *, char ***, size_t *);
static int print_body(struct charset *, int, FILE *, FILE *);
static int print_headers(struct reorder *, struct headers *, FILE *);
static void sigpipe(int);

int
main(int argc, char *argv[])
{
	struct charset charset;
	struct getline gl;
	struct headers headers;
	struct ignore ignore;
	struct imsgbuf msgbuf;
	struct reorder reorder;
	int linewrap;

	if (getdtablecount() != 4)
		errx(1, "extra file descriptors open");

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL)
		err(1, "setlocale");
	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	imsg_init(&msgbuf, PARENT_FD);
	memset(&ignore, 0, sizeof(ignore));
	linewrap = 0;
	memset(&reorder, 0, sizeof(reorder));

	for (;;) {
		struct imsg msg;
		ssize_t n;

		if ((n = imsg_get_blocking(&msgbuf, &msg)) == -1)
			err(1, "imsg_get_blocking");
		if (n == 0)
			break;

		switch (imsg_get_type(&msg)) {
		case IMSG_MDR_IGNORE:
		case IMSG_MDR_RETAIN:
			if (ignore.argc != 0)
				errx(1, "parent sent multiple ignores");
			if (imsg_get_argv(&msg, &ignore.argv, &ignore.argc) == -1)
				err(1, "imsg_get_argv");
			if (imsg_get_type(&msg) == IMSG_MDR_IGNORE)
				ignore.type = IGNORE_IGNORE;
			else
				ignore.type = IGNORE_RETAIN;
			imsg_free(&msg);
			break;
		case IMSG_MDR_IGNOREALL:
			if (ignore.argc != 0)
				errx(1, "parent sent multiple ignores");
			ignore.type = IGNORE_ALL;
			imsg_free(&msg);
			break;
		case IMSG_MDR_LINEWRAP:
			if (imsg_get_data(&msg, &linewrap, sizeof(linewrap)) == -1)
				errx(1, "parent sent bogus data");
			imsg_free(&msg);
			break;
		case IMSG_MDR_REORDER:
			if (reorder.argc != 0)
				errx(1, "parent sent multiple ignores");
			if (imsg_get_argv(&msg, &reorder.argv, &reorder.argc) == -1)
				err(1, "imsg_get_argv");
			imsg_free(&msg);
			break;
		case IMSG_MDR_PIPEOK:
			signal(SIGPIPE, sigpipe);
			imsg_free(&msg);
			break;
		default:
			errx(1, "parent sent bogus imsg");
		}
	}

	charset_set_type(&charset, CHARSET_ASCII);
	encoding_set_type(&charset.encoding, ENCODING_7BIT);
	memset(&gl, 0, sizeof(gl));
	RB_INIT(&headers);

	for (;;) {
		struct header header;
		struct header_rb *fv, *rb;
		int hv;

		if ((hv = header_read(stdin, &gl, &header, 1)) == HEADER_ERR)
			errx(1, "invalid header");
		if (hv == HEADER_EOF)
			break;

		if (!strcasecmp(header.key, "content-transfer-encoding")) {
			if (encoding_set(&charset.encoding, header.val) == -1)
				errx(1, "invalid content-transfer-encoding");
		}
		else if (!strcasecmp(header.key, "content-type")) {
			struct content_type type;

			if (content_type_init(&type, header.val) == -1)
				errx(1, "invalid Content-Type");

			for (;;) {
				struct content_type_var var;
				int n;

				if ((n = content_type_next(&type, &var)) == -1)
					errx(1, "invalid Content-Type variable %s", header.val);
				if (n == 0)
					break;

				if (bounded_strcasecmp("charset", var.key, var.key_len) != 0)
					continue;

				if (charset_set(&charset, var.val, var.val_len) == -1) {
					warnx("invalid charset %.*s", (int)var.val_len, var.val);
					charset_set_type(&charset, CHARSET_OTHER);
				}
			}

			if (!bounded_strcasecmp("multipart", type.type, type.type_len)) {
				/*
				 * Multipart messages are not currently handled,
				 * so it must be assumed that they can contain
				 * data in any character set.
				 */
				charset_set_type(&charset, CHARSET_OTHER);
				encoding_set_type(&charset.encoding, ENCODING_8BIT);
			}
		}

		if (header_ignore(&ignore, header.key)) {
			free(header.key);
			free(header.val);
			continue;
		}

		if ((rb = malloc(sizeof(*rb))) == NULL)
			err(1, NULL);
		rb->key = header.key;

		rb->vals = reallocarray(NULL, 1, sizeof(*rb->vals));
		if (rb->vals == NULL)
			err(1, NULL);
		rb->vals[0] = header.val;
		rb->nval = 1;

		if ((fv = RB_INSERT(headers, &headers, rb)) != NULL) {
			/* duplicate header */

			fv->vals = reallocarray(fv->vals, fv->nval + 1, 
				sizeof(*fv->vals));
			if (fv->vals == NULL)
				err(1, NULL);
			fv->vals[fv->nval++] = rb->vals[0];

			free(rb->key);
			free(rb->vals);
			free(rb);
		}
	}

	if (print_headers(&reorder, &headers, stdout) == -1)
		err(1, "failed to print headers");
	if (print_body(&charset, linewrap, stdin, stdout) == -1)
		errx(1, "failed to print body of letter");

	if (fflush(stdout) == EOF)
		err(1, "fflush");

	free(gl.line);

	for (size_t i = 0; i < reorder.argc; i++)
		free(reorder.argv[i]);
	free(reorder.argv);

	for (size_t i = 0; i < ignore.argc; i++)
		free(ignore.argv[i]);
	free(ignore.argv);

	headers_free(&headers);

	imsg_clear(&msgbuf);
	close(PARENT_FD);
}

static void
headers_free(struct headers *headers)
{
	struct header_rb *h, *h1;

	RB_FOREACH_SAFE(h, headers, headers, h1) {
		RB_REMOVE(headers, headers, h);
		header_free(h);
	}
}

static void
header_free(struct header_rb *rb)
{
	for (size_t i = 0; i < rb->nval; i++)
		free(rb->vals[i]);
	free(rb->vals);
	free(rb->key);
	free(rb);
}

static int
header_ignore(struct ignore *ignore, const char *key)
{
	int rv;

	switch (ignore->type) {
	case IGNORE_ALL:
		return 1;
	case IGNORE_IGNORE:
		rv = 1;
		break;
	case IGNORE_RETAIN:
		rv = 0;
		break;
	default:
		assert(0);
	}

	for (size_t i = 0; i < ignore->argc; i++)
		if (!strcasecmp(ignore->argv[i], key))
			return rv;
	return !rv;
}

static int
header_print(struct header_rb *rb, FILE *out)
{
	for (size_t i = 0; i < rb->nval; i++)
		if (fprintf(out, "%s: %s\n", rb->key, rb->vals[i]) < 0)
			return -1;
	return 0;
}

static int
header_rb_cmp(struct header_rb *one, struct header_rb *two)
{
	return strcasecmp(one->key, two->key);
}

RB_GENERATE_STATIC(headers, header_rb, entries, header_rb_cmp);

static int
imsg_get_argv(struct imsg *msg, char ***out_argv, size_t *out_argc)
{
	struct ibuf ibuf;
	size_t argc;
	char **argv;

	if (imsg_get_ibuf(msg, &ibuf) == -1)
		return -1;

	argc = 0;
	argv = NULL;
	while (ibuf_size(&ibuf) != 0) {
		char *s, **t;

		if (ibuf_get_delim(&ibuf, &s, '\0') == -1)
			goto fail;

		t = reallocarray(argv, argc + 1, sizeof(*argv));
		if (t == NULL) {
			free(s);
			goto fail;
		}

		argv = t;
		argv[argc++] = s;
	}

	*out_argc = argc;
	*out_argv = argv;
	return 0;

	fail:
	for (size_t i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
	return -1;
}

static int
print_body(struct charset *charset, int linewrap, FILE *in, FILE *out)
{
	int lastnl;

	lastnl = 0;
	for (;;) {
		char buf[4];
		int n;

		if ((n = charset_getchar(in, charset, buf)) == -1)
			return -1;
		if (n == 0)
			return 0;

		if (n == 1 && buf[0] == '\n')
			lastnl = 0;
		else
			lastnl++;

		if (linewrap != 0 && lastnl >= linewrap && n == 1 
			&& isspace((unsigned char)buf[0])) {
				if (fputc('\n', out) == EOF)
					return -1;
				lastnl = 0;
				continue;
		}

		if (fwrite(buf, n, 1, out) != 1)
			return -1;
	}
}

static int
print_headers(struct reorder *reorder, struct headers *headers, FILE *out)
{
	struct header_rb *h;
	int any;

	any = 0;
	for (size_t i = 0; i < reorder->argc; i++) {
		struct header_rb find, *found;

		find.key = reorder->argv[i];
		if ((found = RB_FIND(headers, headers, &find)) != NULL) {
			if (header_print(found, out) == -1)
				return -1;
			RB_REMOVE(headers, headers, found);
			header_free(found);
			any = 1;
		}
	}

	RB_FOREACH(h, headers, headers) {
		if (header_print(h, out) == -1)
			return -1;
		any = 1;
	}

	if (any)
		if (fputc('\n', out) == EOF)
			return -1;

	return 0;
}

static void
sigpipe(int signo)
{
	/* avoid WIFSIGNALED */
	_exit(0);
}
