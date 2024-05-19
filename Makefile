PROG = mail
CFLAGS = -g
SRCS = mail.c mail-util.c maildir.c
NOMAN = noman
TIDYFLAGS = -checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

.include <bsd.prog.mk>

.PHONY: tidy

tidy:
	clang-tidy ${TIDYFLAGS} ${SRCS}
