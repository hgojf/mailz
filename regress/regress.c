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

#include <stdio.h>

#include "charset.h"
#include "content-proc.h"
#include "encoding.h"
#include "header.h"
#include "maildir.h"

int
main(void)
{
	charset_getc_test();
	content_proc_letter_test();
	content_proc_summary_test();
	encoding_getc_test();
	header_address_test();
	header_date_test();
	header_encoding_test();
	header_lex_test();
	header_message_id_test();
	header_name_test();
	header_subject_test();
	header_subject_reply_test();
	maildir_get_flag_test();
	maildir_set_flag_test();
	maildir_unset_flag_test();

	puts("Ok.");
}
