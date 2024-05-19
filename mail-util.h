#ifndef MAILZ_MAIL_UTIL_H
#define MAILZ_MAIL_UTIL_H
void mail_free(struct mail *);
void letter_free(struct letter *);
const char *header_find(struct letter *, char *);
#endif /* MAILZ_MAIL_UTIL_H */
