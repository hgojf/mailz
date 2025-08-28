/*
 * Copyright (c) 2025 Henry Ford <fordhenry2299@gmail.com>

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

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../header.h"
#include "header.h"

#define nitems(a) (sizeof((a)) / sizeof(*(a)))

void
header_address_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *addr;
		const char *name;
		size_t addrsz;
		size_t namesz;
		int error;
	} tests[] = {
		#define test_addr(in, addr) { in, addr, "", 255, 0, HEADER_OK }
		#define test_bad(in) { in, NULL, NULL, 255, 65, HEADER_INVALID }
		#define test_eof(in) { in, NULL, NULL, 255, 65, HEADER_EOF }
		#define test_lim(in, addr, name) { in, addr, name, \
						   sizeof(addr), sizeof(name), \
						   HEADER_OK }
		#define test_off(in, addr, name) { in, addr, name, \
						   sizeof(addr) - 1,sizeof(name) - 1, \
						   HEADER_INVALID }
		#define test_ok(in, addr, name)  { in, addr, name, \
						   255, 65, HEADER_OK }
		test_addr("Dave <dave@fake.invalid>", "dave@fake.invalid"),
		test_ok("Dave <dave@fake.invalid>", "dave@fake.invalid", "Dave"),
		test_ok("\"Bond, James\" <dave@fake.invalid>", "dave@fake.invalid", "Bond, James"),
		test_ok("dave@fake.invalid", "dave@fake.invalid", ""),
		test_ok("dave@fake.invalid   ", "dave@fake.invalid", ""),
		test_ok("<dave@fake.invalid>", "dave@fake.invalid", ""),

		test_bad("<>"),
		test_bad(","),
		test_eof("\n"),

		test_bad("Dave <"),

		test_lim("Dave <dave@fake.invalid>", "dave@fake.invalid", "Dave"),
		test_off("Dave <dave@fake.invalid>", "dave@fake.invalid", "Dave"),

		#undef test_addr
		#undef test_bad
		#undef test_lim
		#undef test_off
		#undef test_ok
	};

	for (i = 0; i < nitems(tests); i++) {
		struct header_address address;
		FILE *fp;
		char addr[255], name[65];
		int eof, error;

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		address.addr = addr;
		address.addrsz = tests[i].addrsz;
		address.name = name;
		address.namesz = tests[i].namesz;

		if (tests[i].namesz == 0)
			name[0] = '\0';

		eof = 0;
		error = header_address(fp, &address, &eof);
		if (error != tests[i].error)
			errx(1, "wrong error %d", error);
		if (error == HEADER_OK) {
			if (strcmp(addr, tests[i].addr) != 0)
				errx(1, "wrong address");
			if (strcmp(name, tests[i].name) != 0)
				errx(1, "wrong name");
		}

		fclose(fp);
	}
}

void
header_content_type_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *type;
		const char *subtype;
		size_t typesz;
		size_t subtypesz;
		int error;
	} tests[] = {
		{ "text/plain", "text", "plain", 10, 10, HEADER_OK },
		{ "text/plain;", "text", "plain", 10, 10, HEADER_OK },

		{ "text/plain", "text", "plain", 5, 6, HEADER_OK },
		{ "text/plain", "text", NULL, 5, 5, HEADER_OK },
		{ "text/plain", NULL, NULL, 4, 5, HEADER_OK },

		{ "text", NULL, NULL, 10, 10, HEADER_INVALID },
	};

	for (i = 0; i < nitems(tests); i++) {
		struct content_type ct;
		char subtype[10], type[10];
		FILE *fp;
		int eof, error;

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		ct.type = type;
		ct.typesz = tests[i].typesz;
		ct.subtype = subtype;
		ct.subtypesz = tests[i].subtypesz;

		eof = 0;
		error = header_content_type(fp, NULL, &ct, &eof);
		if (error != tests[i].error)
			errx(1, "wrong error");
		if (error == HEADER_OK) {
			if ((tests[i].type == NULL) != ct.type_trunc)
				errx(1, "wrong type");
			if (tests[i].type != NULL)
				if (strcmp(type, tests[i].type) != 0)
					errx(1, "wrong type");

			if ((tests[i].subtype == NULL) != ct.subtype_trunc)
				errx(1, "wrong type");
			if (tests[i].subtype != NULL)
				if (strcmp(subtype, tests[i].subtype) != 0)
					errx(1, "wrong subtype");
		}

		fclose(fp);
	}
}

void
header_content_type_var_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *var;
		const char *val;
		size_t varsz;
		size_t valsz;
		int error;
	} tests[] = {
		{ "charset=us-ascii", "charset", "us-ascii", 20, 20, HEADER_OK },
		{ "charset=us-ascii;", "charset", "us-ascii", 20, 20, HEADER_OK },

		{ "charset=us-ascii;", "charset", "us-ascii", 8, 9, HEADER_OK },
		{ "charset=us-ascii", "charset", NULL, 20, 8, HEADER_OK },
		{ "charset=us-ascii", NULL, NULL, 7, 8, HEADER_OK },

		{ "charset", NULL, NULL, 20, 20, HEADER_INVALID },
	};

	for (i = 0; i < nitems(tests); i++) {
		struct content_type_var vt;
		char val[20], var[20];
		FILE *fp;
		int eof, error;

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		vt.val = val;
		vt.valsz = tests[i].valsz;
		vt.var = var;
		vt.varsz = tests[i].varsz;

		eof = 0;
		error = header_content_type_var(fp, NULL, &vt, &eof);
		if (error != tests[i].error)
			errx(1, "wrong error");
		if (error == HEADER_OK) {
			if ((tests[i].var == NULL) != vt.var_trunc)
				errx(1, "wrong var");
			if (tests[i].var != NULL)
				if (strcmp(var, tests[i].var) != 0)
					errx(1, "wrong var");

			if ((tests[i].val == NULL) != vt.val_trunc)
				errx(1, "wrong val");
			if (tests[i].val != NULL)
				if (strcmp(val, tests[i].val) != 0)
					errx(1, "wrong val");
		}

		fclose(fp);
	}
}

void
header_copy_addresses_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *exclude;
		const char *out;
		int error;
	} tests[] = {
		{ "dave@bogus.invalid, Thomas <tom@bogus.invalid>",
		   "tom@bogus.invalid", " dave@bogus.invalid",
		   HEADER_OK },
		{ "dave@bogus.invalid, Thomas <tom@bogus.invalid>, Fredrick <fred@bogus.invalid>",
		   "tom@bogus.invalid",
		   " dave@bogus.invalid, Fredrick <fred@bogus.invalid>",
		   HEADER_OK },
	};

	for (i = 0; i < nitems(tests); i++) {
		FILE *in, *out;
		char *obuf;
		size_t osz;
		int any, error;

		in = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (in == NULL)
			err(1, "fmemopen");

		out = open_memstream(&obuf, &osz);
		if (out == NULL)
			err(1, "open_memstream");

		any = 0;
		error = header_copy_addresses(in, out, tests[i].exclude,
					      &any);
		if (error != tests[i].error)
			errx(1, "wrong error");

		if (fclose(out) == EOF)
			err(1, "fclose");
		if (error == HEADER_OK) {
			if (strcmp(obuf, tests[i].out) != 0)
				errx(1, "wrong output");
		}

		free(obuf);
		fclose(in);
	}
}

void
header_date_test(void)
{
	size_t i;
	const struct {
		char *in;
		time_t date;
		int error;
	} tests[] = {
		{ "Mon, 01 Jan 1970 00:00:00 -0000", 0, HEADER_OK },
		{ "Mon, 01 Jan 1970 01:00:00 +0100", 0, HEADER_OK },
		{ "Mon, 01 Jan 1970 00:00 -0000", 0, HEADER_OK },
		{ "Mon, 01 Jan 1970 00:00:00 GMT", 0, HEADER_OK },
		{ "Mon, 01 Jan 70 00:00:00 -0000", 0, HEADER_OK },

		{ "Monday, 01 Jan 1970 00:00:00 -0000", 0, HEADER_INVALID },
		{ "Mon, 01 January 1970 00:00:00 -0000", 0, HEADER_INVALID },
		{ "Mon, 01 January 1970 00:00:00 ESD", 0, HEADER_INVALID },
	};

	for (i = 0; i < nitems(tests); i++) {
		FILE *fp;
		int error;
		time_t date;

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		error = header_date(fp, &date);
		if (error != tests[i].error)
			errx(1, "wrong error");
		if (error == HEADER_OK)
			if (date != tests[i].date)
				errx(1, "wrong date");

		fclose(fp);
	}
}

void
header_encoding_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *out;
		size_t bufsz;
		int error;
	} tests[] = {
		{ "Quoted-Printable", "Quoted-Printable", 20, HEADER_OK },
		{ "Quoted-Printable", "Quoted-Printable", 17, HEADER_OK },
		{ "Quoted-Printable", "Quoted-Printable", 16, HEADER_TRUNC },
	};

	for (i = 0; i < nitems(tests); i++) {
		char buf[20];
		FILE *fp;
		int error;

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		error = header_encoding(fp, NULL, buf, tests[i].bufsz);
		if (error != tests[i].error)
			errx(1, "wrong error");
		if (error == HEADER_OK)
			if (strcmp(buf, tests[i].out) != 0)
				errx(1, "wrong output");

		fclose(fp);
	}
}

void
header_lex_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *out;
		int raw;
		int error;
	} tests[] = {
		{ "hi", "hi", 0, HEADER_EOF },
		{ "hi\n there", "hi there", 0, HEADER_EOF },
		{ "hi(there)", "hi", 0, HEADER_EOF },
		{ "hi \"there\"", "hi there", 0, HEADER_EOF },
		{ "hi(\"", "hi(\"", 1, HEADER_EOF },
		{ "\n", "", 1, HEADER_EOF },

		{ "hi(", "hi", 0, HEADER_INVALID },
		{ "hi\"", "hi", 0, HEADER_INVALID },
	};

	for (i = 0; i < nitems(tests); i++) {
		struct header_lex lex;
		FILE *fp;
		const char *out;
		int error;

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		lex.echo = NULL;
		lex.cstate = tests[i].raw ? -1 : 0;
		lex.qstate = tests[i].raw ? -1 : 0;
		lex.skipws = tests[i].raw ? 0 : 1;

		out = tests[i].out;
		while ((error = header_lex(fp, &lex)) != tests[i].error) {
			if (error < 0)
				errx(1, "wrong error");
			if (*out == '\0' || *out++ != error)
				errx(1, "wrong output");
		}
		if (*out != '\0')
			errx(1, "wrong output");

		if (tests[i].error == HEADER_OK && !feof(fp))
			errx(1, "all input not consumed");

		fclose(fp);
	}
}

void
header_lex_echo_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *echo;
	} tests[] = {
		{ "hello", "hello\n" },
		{ "hello\x1b", "hello\n" },
	};

	for (i = 0; i < nitems(tests); i++) {
		struct header_lex lex;
		FILE *echo_in, *echo_out, *fp;
		const char *echo;
		int error, p[2];

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		if (pipe2(p, O_CLOEXEC) == -1)
			err(1, "pipe2");
		if ((echo_in = fdopen(p[0], "r")) == NULL)
			err(1, "fdopen");
		if ((echo_out = fdopen(p[1], "w")) == NULL)
			err(1, "fdopen");

		lex.echo = echo_out;
		lex.cstate = 0;
		lex.qstate = 0;
		lex.skipws = 0;

		while ((error = header_lex(fp, &lex)) != HEADER_EOF)
			if (error < 0)
				errx(1, "bad input");

		fclose(echo_out);
		for (echo = tests[i].echo; *echo != '\0'; echo++) {
			int ch;

			if ((ch = fgetc(echo_in)) == EOF)
				errx(1, "early end of file");
			if (ch != *echo)
				errx(1, "wrong output");
		}
		if (fgetc(echo_in) != EOF)
			errx(1, "too much output");

		fclose(echo_in);
		fclose(fp);
	}
}

void
header_message_id_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *out;
		size_t bufsz;
		int error;
	} tests[] = {
		{ "<uniq>", "uniq", 10, HEADER_OK },
		{ "<uniq>", "uniq", 5, HEADER_OK },

		{ "<hello\xFF>", NULL, 10, HEADER_INVALID },
		{ "hello<uniq", NULL, 10, HEADER_INVALID },
		{ "<uniq", NULL, 10, HEADER_INVALID },
		{ "<uniq>", NULL, 4, HEADER_INVALID },
	};

	for (i = 0; i < nitems(tests); i++) {
		char buf[10];
		FILE *fp;
		int error;

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		error = header_message_id(fp, buf, tests[i].bufsz);
		if (error != tests[i].error)
			errx(1, "wrong error %d", error);
		if (error == HEADER_OK)
			if (strcmp(buf, tests[i].out) != 0)
				errx(1, "wrong output");

		fclose(fp);
	}
}

void
header_name_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *out;
		size_t bufsz;
		int error;
	} tests[] = {
		{ "Cc:", "Cc", 10, HEADER_OK },

		{ "\n", NULL, 10, HEADER_EOF },

		{ "Cc", NULL, 10, HEADER_INVALID },
		{ "Cc\xFF:", NULL, 10, HEADER_INVALID },
		{ "Cc:", NULL, 2, HEADER_INVALID },
	};

	for (i = 0; i < nitems(tests); i++) {
		FILE *fp;
		char buf[10];
		int error;

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		error = header_name(fp, buf, tests[i].bufsz);
		if (error != tests[i].error)
			errx(1, "wrong error");
		if (error == HEADER_OK)
			if (strcmp(buf, tests[i].out) != 0)
				errx(1, "wrong output");

		fclose(fp);
	}
}

void
header_subject_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *out;
		size_t bufsz;
	} tests[] = {
		{ "hi", "hi", 10 },
		{ "h\xFFi", "hi", 10 },
		{ "hello", "hello", 6 },
		{ "hello", "hell", 5 },
	};

	for (i = 0; i < nitems(tests); i++) {
		FILE *fp;
		char buf[10];
		int error;

		fp = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (fp == NULL)
			err(1, "fmemopen");

		error = header_subject(fp, buf, tests[i].bufsz);
		if (error != HEADER_OK)
			errx(1, "wrong error");
		if (strcmp(buf, tests[i].out) != 0)
			errx(1, "wrong output");

		fclose(fp);
	}
}

void
header_subject_reply_test(void)
{
	size_t i;
	const struct {
		char *in;
		const char *out;
	} tests[] = {
		{ "hi", "Subject: Re: hi\n" },
		{ "Re: hi", "Subject: Re: hi\n" },
		{ "Resurrection", "Subject: Re: Resurrection\n" },
	};

	for (i = 0; i < nitems(tests); i++) {
		FILE *in, *out;
		char *obuf;
		size_t osize;
		int error;

		in = fmemopen(tests[i].in, strlen(tests[i].in),
			      "r");
		if (in == NULL)
			err(1, "fmemopen");

		out = open_memstream(&obuf, &osize);
		if (out == NULL)
			err(1, "open_memstream");

		error = header_subject_reply(in, out);
		if (error != HEADER_OK)
			errx(1, "wrong error");

		fflush(out);
		if (strcmp(obuf, tests[i].out) != 0)
			errx(1, "wrong output");

		fclose(in);
		fclose(out);
		free(obuf);
	}
}
