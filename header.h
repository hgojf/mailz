#ifndef HEADER_H
#define HEADER_H

#define HEADER_OK 0
#define HEADER_EOF -1
#define HEADER_INVALID -2
#define HEADER_OUTPUT -3

struct header_lex {
	int cstate;
	int qstate;
	int skipws;
	FILE *echo;
};

int header_name(FILE *, char *, size_t);
int header_lex(FILE *, struct header_lex *);
int header_skip(FILE *, FILE *);
int header_subject(FILE *, char *, size_t);
int header_subject_reply(FILE *, FILE *);

#endif /* HEADER_H */
