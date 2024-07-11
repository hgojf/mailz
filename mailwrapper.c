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

#include <err.h>
#include <string.h>
#include <unistd.h>

/* 
 * Allows mailz to execute programs in specifically the way it needs to.
 * Not to be confused with the traditional mailwrapper
 */
int
main(int argc, char *argv[])
{
	if (strcmp(argv[0], "vi") == 0) {
		if (argc != 2)
			errx(1, "invalid usage");
		execl("/usr/bin/vi", "vi", "--", argv[1], NULL);
		err(1, "execl");
	}
	else if (strcmp(argv[0], "sendmail") == 0) {
		if (argc != 2)
			errx(1, "invalid usage");
		execl("/usr/sbin/sendmail", "sendmail", "-t", "--", NULL);
		err(1, "execl");
	}
	else if (strcmp(argv[0], "less") == 0) {
		if (argc != 2)
			errx(1, "invalid usage");
		execl("/usr/bin/less", "less", "--", argv[1], NULL);
		err(1, "execl");
	}
}
