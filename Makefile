.POSIX:

.PHONY: clean install tidy

CFLAGS = -MD -MP -O2 -pipe -g
TIDYCHECKS = \
	-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling
SRCS = date.c mail.c mailbox.c strtonum.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)
PREFIX ?= /usr/local

mail: ${OBJS}
	$(CC) -o $@ ${LDFLAGS} ${OBJS}

clean:
	rm -f ${DEPS} ${OBJS} mail

install:
	$(INSTALL) -m 0755 mail ${PREFIX}/bin/mailz

tidy:
	clang-tidy -checks=${TIDYCHECKS} ${SRCS}

-include ${DEPS}
