.POSIX:

.PHONY: all clean install install-man test tidy

PREFIX ?= /usr/local
CFLAGS = -O2 -pipe -g -MD -MP
TIDYCHECKS = \
	-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

SRCS = address.c mail.c mailbox.c sendmail.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

CTAGS ?= ctags
INSTALL ?= install

all: mail mailwrapper

mail: ${OBJS}
	$(CC) -o $@ ${LDFLAGS} ${OBJS}

mailwrapper: mailwrapper.o
	$(CC) -o $@ ${CFLAGS} ${LDFLAGS} mailwrapper.o

clean:
	rm -f ${DEPS} ${OBJS} ${DEPS_COMPAT} ${OBJS_COMPAT} mail \
	mailwrapper mailwrapper.o mailwrapper.d regress.o regress

install:
	$(INSTALL) -m 0755 mail ${PREFIX}/bin/mailz
	$(INSTALL) -m 0755 mailwrapper ${PREFIX}/libexec/mailzwrapper

install-man:
	$(INSTALL) -m 0644 mailz.1 ${PREFIX}/man/man1/

regress: address.c mailbox.o sendmail.o regress.o
	$(CC) -o $@ ${LDFLAGS} address.c mailbox.o sendmail.o regress.o

tags: ${SRCS}
	$(CTAGS) ${SRCS}

test: regress
	@./regress

tidy:
	clang-tidy -checks=${TIDYCHECKS} ${SRCS}

-include ${DEPS} regress.d
