#ifndef MAILZ_IMSG_UTIL_H
#define MAILZ_IMSG_UTIL_H
int ibuf_get_string(struct ibuf *, char **);
int imsg_flush_blocking(struct imsgbuf *);
ssize_t imsg_get_blocking(struct imsgbuf *, struct imsg *);
#endif /* MAILZ_IMSG_UTIL_H */
