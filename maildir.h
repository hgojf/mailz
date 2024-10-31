#ifndef MAILZ_MAILDIR_H
#define MAILZ_MAILDIR_H
int maildir_get_flag(const char *, int);
const char *maildir_set_flag(const char *, int, char *, size_t);
const char *maildir_unset_flag(const char *, int, char *, size_t);
#endif /* MAILZ_MAILDIR_H */
