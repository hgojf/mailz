#ifndef HEADER_H
#define HEADER_H

#define HEADER_OK 0
#define HEADER_EOF -1
#define HEADER_INVALID -2

int header_name(FILE *, char *, size_t);

#endif /* HEADER_H */
