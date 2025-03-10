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

#ifndef CONTENT_H
#define CONTENT_H
enum {
	IMSG_CNT_IGNORE,
	IMSG_CNT_OK,
	IMSG_CNT_RETAIN,
	IMSG_CNT_LETTER,
	IMSG_CNT_LETTERPIPE,
	IMSG_CNT_REPLY,
	IMSG_CNT_REPLYPIPE,
	IMSG_CNT_SUMMARY
};

#define CNT_PFD 3

struct content_header {
	char name[996];
};

struct content_reply_setup {
	char addr[255];
	int group;
};

struct content_summary {
	time_t date;
	char from[255];
	char subject[245];
	int have_subject;
};
#endif /* ! CONTENT_H */
