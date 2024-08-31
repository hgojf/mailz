#ifndef MAILZ_SEND_H
#define MAILZ_SEND_H
int reply(int, struct address *, struct letter *);
int send(const char *, int, char **, FILE *, FILE *, struct address *);
#endif /* MAILZ_SEND_H */
