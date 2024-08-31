#ifndef MAILZ_EXTRACT_H
#define MAILZ_EXTRACT_H
#include <sys/queue.h>

#include <imsg.h>
#include <stdio.h>
#include <unistd.h>

#include "maildir-extract.h"

struct extract {
	struct imsgbuf msgbuf;
	FILE *e;
	int fd;
	pid_t pid;
};

struct extracted_header {
	char *key;
	enum extract_header_type type;
	union extract_header_val {
		/* EXTRACT_DATE */
		time_t date;
		/* EXTRACT_FROM */
		struct {
			char *addr;
			char *name;
		} from;
		/* EXTRACT_MESSAGE_ID, EXTRACT_STRING */
		char *string;
	} val;
};

void extract_header_free(enum extract_header_type, 
	union extract_header_val *);

int maildir_extract(struct extract *, struct extracted_header *, size_t);
int maildir_extract_close(struct extract *);

int maildir_extract_next(struct extract *, int, 
	struct extracted_header *, size_t);

int maildir_extract_quick(int, struct extracted_header *, size_t);
#endif /* MAILZ_EXTRACT_H */
