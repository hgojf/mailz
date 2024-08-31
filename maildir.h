#ifndef MAILZ_MAILDIR_H
#define MAILZ_MAILDIR_H
int maildir_letter_seen(const char *);
char *maildir_setflag(char *, char);
char *maildir_unsetflag(char *, char);
#endif /* MAILZ_MAILDIR_H */
