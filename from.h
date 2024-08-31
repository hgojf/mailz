#ifndef MAILZ_FROM_H
#define MAILZ_FROM_H
struct from {
	size_t al;
	size_t nl;
	char *addr;
	char *name;
};

int from_parse(char *, struct from *);
#endif /* MAILZ_FROM_H */
