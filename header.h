#ifndef MAILZ_HEADER_H
#define MAILZ_HEADER_H
struct getline {
	char *line;
	size_t n;
};

struct header {
	char *key;
	char *val;
};

#define HEADER_ERR 1
#define HEADER_EOF 2
int header_read(FILE *, struct getline *, struct header *, int);
#endif /* MAILZ_HEADER_H */
