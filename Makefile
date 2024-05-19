PROG = mail
CFLAGS = -g
SRCS = mail.c mail-util.c maildir.c
NOMAN = noman

.include <bsd.prog.mk>

.PHONY: tidy

tidy:
	clang-tidy ${SRCS}
