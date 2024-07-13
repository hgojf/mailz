#include "errstr.h"
#include "maildir-send.h"
#include "maildir-setup.h"

const char *
maildir_setup_errstr(int n)
{
	enum maildir_setup_status nth = n;

	switch (nth) {
	case MAILDIR_SETUP_OK:
		return "no error";
	case MAILDIR_SETUP_USAGE:
		return "invalid usage";
	case MAILDIR_SETUP_UNVEIL:
		return "unveil failed";
	case MAILDIR_SETUP_PLEDGE:
		return "pledge failed";
	case MAILDIR_SETUP_OPENMAIN:
		return "failed to open maildir";
	case MAILDIR_SETUP_OPENCUR:
		return "failed to open cur directory of maildir";
	case MAILDIR_SETUP_OPENNEW:
		return "failed to open new directory of maildir";
	case MAILDIR_SETUP_FDOPENNEW:
		return "failed to fdopen new directory of maildir";
	case MAILDIR_SETUP_READDIR:
		return "readdir failed";
	case MAILDIR_SETUP_SNPRINTF:
		return "snprintf failed";
	case MAILDIR_SETUP_TOOLONG:
		return "entry name was too long";
	case MAILDIR_SETUP_RENAME:
		return "failed to rename entry";
	case MAILDIR_SETUP_CLOSE:
		return "failed to close a file";
	case MAILDIR_SETUP_WRITE:
		return "failed to write errno";
	case MAILDIR_SETUP_SWRITE:
		return "failed to write errno due to short write";
	case MAILDIR_SETUP_PIPE:
		return "failed to create pipe";
	case MAILDIR_SETUP_FORK:
		return "failed to fork";
	case MAILDIR_SETUP_DUP:
		return "failed to pipe standard streams to /dev/null";
	case MAILDIR_SETUP_EXEC:
		return "failed to exec maildir-setup";
	case MAILDIR_SETUP_WAITPID:
		return "waitpid failed";
	case MAILDIR_SETUP_READ:
		return "failed to read errno";
	case MAILDIR_SETUP_SREAD:
		return "failed to read errno due to short read";
	}

	return "undefined error";
}

const char *
maildir_send_errstr(int n)
{
	enum maildir_send_status nth = n;

	switch (nth) {
	case MAILDIR_SEND_OK:
		return "no error";
	case MAILDIR_SEND_USAGE:
		return "invalid usage";
	case MAILDIR_SEND_WRITE:
		return "failed to write errno";
	case MAILDIR_SEND_SWRITE:
		return "failed to write errno due to short write";
	case MAILDIR_SEND_UNVEIL:
		return "unveil failed";
	case MAILDIR_SEND_PLEDGE:
		return "pledge failed";
	case MAILDIR_SEND_PIPE:
		return "failed to create a pipe";
	case MAILDIR_SEND_FORK:
		return "failed to fork";
	case MAILDIR_SEND_DUP:
		return "failed to pipe standard streams to /dev/null";
	case MAILDIR_SEND_CLOSE:
		return "failed to close a file";
	case MAILDIR_SEND_EXEC:
		return "failed to exec";
	case MAILDIR_SEND_FDOPEN:
		return "fdopen failed";
	case MAILDIR_SEND_PRINTF:
		return "printf failed";
	case MAILDIR_SEND_GETC:
		return "getc failed";
	case MAILDIR_SEND_WAITPID:
		return "waitpid failed";
	case MAILDIR_SEND_SENDMAIL:
		return "sendmail failed";
	case MAILDIR_SEND_PUTC:
		return "putc failed";
	case MAILDIR_SEND_READ:
		return "reading errno failed";
	case MAILDIR_SEND_SREAD:
		return "reading errno failed due to a short read";
	}

	return "undefined error";
}
