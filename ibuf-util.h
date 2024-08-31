#ifndef MAILZ_IBUF_UTIL_H
#define MAILZ_IBUF_UTIL_H
int ibuf_get_delim(struct ibuf *, char **, int);
int ibuf_get_string(struct ibuf *, char **, size_t);
#endif /* MAILZ_IBUF_UTIL_H */
