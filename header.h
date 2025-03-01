#ifndef HEADER_H
#define HEADER_H

#define HEADER_OK 0
#define HEADER_EOF -1
#define HEADER_INVALID -2
#define HEADER_OUTPUT -3
#define HEADER_TRUNC -4

struct header_address {
	char *addr;
	char *name;
	size_t addrsz;
	size_t namesz;
};

struct header_lex {
	int cstate;
	int qstate;
	int skipws;
	FILE *echo;
};

int header_address(FILE *, struct header_address *, int *);
int header_copy(FILE *, FILE *);
int header_copy_addresses(FILE *, FILE *, const char *, int *);
int header_date(FILE *, time_t *);
int header_encoding(FILE *, FILE *, char *, size_t);
int header_name(FILE *, char *, size_t);
int header_message_id(FILE *, char *, size_t);
int header_lex(FILE *, struct header_lex *);
int header_skip(FILE *, FILE *);
int header_subject(FILE *, char *, size_t);
int header_subject_reply(FILE *, FILE *);

#endif /* HEADER_H */
