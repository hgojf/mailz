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
//#define PATH_SENDMAIL "/bin/cat"
#define PATH_TMPDIR "/tmp/mailz"
#endif /* MAILZ_PATHNAMES_H */
