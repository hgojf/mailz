#ifndef MAILZ_MAILDIR_EXTRACT_H
#define MAILZ_MAILDIR_EXTRACT_H
/* parent -> maildir-extract */
enum {
	IMSG_MDE_HEADERDEF,
	IMSG_MDE_LETTER,
};

/* maildir-extract -> parent */
enum {
	IMSG_MDE_HEADER,
	IMSG_MDE_HEADERDONE,
};

enum extract_header_type {
	EXTRACT_DATE,
	EXTRACT_FROM,
	EXTRACT_MESSAGE_ID,
	EXTRACT_STRING,
};
#endif /* MAILZ_MAILDIR_EXTRACT_H */
