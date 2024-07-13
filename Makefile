.POSIX:

.PHONY: all clean install install-man test tidy

PREFIX ?= /usr/local
CFLAGS = -O0 -pipe -g -MD -MP
TIDYCHECKS = \
	-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

SRCS = 	address.c command.c command-lex.c errstr.c header.c mail.c \
		maildir.c maildir-cache-read.c maildir-read.c \
		maildir-read-letter.c maildir-send.c maildir-setup.c utf8.c
OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)

PROGS = mail maildir-cache-read maildir-read-letter maildir-read \
	maildir-send maildir-setup

CTAGS ?= ctags
INSTALL ?= install

all: ${PROGS}

mail: address.o command.o command-lex.o errstr.o mail.o maildir.o utf8.o
	$(CC) -o $@ ${LDFLAGS} address.o command.o command-lex.o errstr.o mail.o maildir.o utf8.o

maildir-cache-read: maildir-cache-read.o
	$(CC) -o $@ ${LDFLAGS} maildir-cache-read.o

maildir-read-letter: header.o maildir-read-letter.o utf8.o
	$(CC) -o $@ ${LDFLAGS} header.o maildir-read-letter.o utf8.o

maildir-read: address.o header.o maildir-read.o
	$(CC) -o $@ ${LDFLAGS} address.o header.o maildir-read.o

maildir-send: maildir-send.o
	$(CC) -o $@ ${LDFLAGS} maildir-send.o

maildir-setup: maildir-setup.o
	$(CC) -o $@ ${LDFLAGS} maildir-setup.o

command.c: $(.CURDIR)/command.y
	yacc -do command.c $(.CURDIR)/command.y

command-lex.c: $(.CURDIR)/command.l
	lex -o command-lex.c $(.CURDIR)/command.l

clean:
	rm -f ${DEPS} ${OBJS} ${PROGS} command-lex.c command.c command.h

install:
	$(INSTALL) -m 0755 mail ${PREFIX}/bin/mailz
	$(INSTALL) -m 0755 maildir-read-letter ${PREFIX}/libexec/mailz-maildir-read-letter
	$(INSTALL) -m 0755 maildir-read ${PREFIX}/libexec/mailz-maildir-read
	$(INSTALL) -m 0755 maildir-send ${PREFIX}/libexec/mailz-maildir-send
	$(INSTALL) -m 0755 maildir-setup ${PREFIX}/libexec/mailz-maildir-setup

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
