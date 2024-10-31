#ifndef MAILZ_IMSG_BLOCKING_H
#define MAILZ_IMSG_BLOCKING_H
int imsg_flush_blocking(struct imsgbuf *);
ssize_t imsg_get_blocking(struct imsgbuf *, struct imsg *);
#endif /* MAILZ_IMSG_BLOCKING_H */
