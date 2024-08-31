#ifndef MAILZ_DATE_H
#define MAILZ_DATE_H
#define EMAIL_DATE_LEN 33

int date_format(struct tm *, long, char [static EMAIL_DATE_LEN]);
time_t date_parse(char *);
#endif /* MAILZ_DATE_H */
