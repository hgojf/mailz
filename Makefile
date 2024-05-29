PROG = mail
CFLAGS = -g -Wall -Wextra
SRCS = date.c commands.c mail.c maildir.c util.c
NOMAN = noman
TIDYFLAGS = -checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

realinstall:
	install -m 0755 mail /usr/local/bin/mailz

.include <bsd.prog.mk>

.PHONY: tidy

tidy:
	clang-tidy ${TIDYFLAGS} ${SRCS}
