#ifndef MAILZ_MAILDIR_H
#define MAILZ_MAILDIR_H
DIR *maildir_setup(int);
int maildir_read(DIR *, struct mail *, const struct options *);
#endif /* MAILZ_MAILDIR_H */
