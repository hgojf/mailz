.POSIX:

.PHONY: all clean install tidy

PREFIX ?= /usr/local
CFLAGS = -O2 -pipe -g
TIDYCHECKS = \
	-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling
SRCS = date.c mail.c mailbox.c sendmail.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

include config.mk

all: mail mailwrapper

mail: ${OBJS}
	$(CC) -o $@ ${LDFLAGS} ${OBJS}

mailwrapper: mailwrapper.c
	$(CC) -o $@ ${CFLAGS} ${LDFLAGS} mailwrapper.c

clean:
	rm -f ${DEPS} ${OBJS} mail

install:
	$(INSTALL) -m 0755 mail ${PREFIX}/bin/mailz
	$(INSTALL) -m 0755 mailwrapper ${PREFIX}/libexec/mailzwrapper

tags: ${SRCS}
	$(CTAGS) ${SRCS}

tidy:
	clang-tidy -checks=${TIDYCHECKS} ${SRCS}

-include ${DEPS}
