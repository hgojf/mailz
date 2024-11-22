#ifndef MAILZ__ERR_H
#define MAILZ__ERR_H
__dead void _err(int, const char *, ...)
	__attribute__((__format__(printf, 2, 3)));
#endif /* MAILZ__ERR_H */
