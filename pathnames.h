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

#ifndef MAILZ_PATHNAMES_H
#define MAILZ_PATHNAMES_H
#define PATH_LESS "/usr/bin/less"

#ifdef REGRESS
#define PATH_MAILDIR_EXTRACT "./maildir-extract/obj/maildir-extract"
#define PATH_MAILDIR_READ_LETTER "./maildir-read-letter/obj/maildir-read-letter"
#else
#define PATH_MAILDIR_EXTRACT "/usr/local/libexec/maildir-extract"
#define PATH_MAILDIR_READ_LETTER "/usr/local/libexec/maildir-read-letter"
#endif /* REGRESS */

#define PATH_SENDMAIL "/usr/sbin/sendmail"
#define PATH_TMPDIR "/tmp/mailz"
#endif /* MAILZ_PATHNAMES_H */
