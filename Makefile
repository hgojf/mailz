DEBUG = -g -fno-inline-functions
CFLAGS = -O2 -pipe
CFLAGS += -MD -MP 
CFLAGS += -I${.CURDIR}/ 
CFLAGS += ${DEBUG}

CFLAGS += -Wall -Wpointer-arith -Wuninitialized -Wstrict-prototypes
CFLAGS += -Wmissing-prototypes -Wunused -Wsign-compare
CFLAGS += -Wshadow -Wdeclaration-after-statement

PREFIX = /usr/local

SRCS_MAILZ = cache.c commands.c leak.c imsg-util.c letter.c lex.c mailz.c parse.c
SRCS_MAILZ += printable.c read-letter.c read-letters.c send.c setup.c
OBJS_MAILZ = $(SRCS_MAILZ:.c=.o)
LDFLAGS_MAILZ = -lutil

SRCS_MAILDIR_READ = header.c leak.c maildir-read.c
OBJS_MAILDIR_READ = $(SRCS_MAILDIR_READ:.c=.o)
LDFLAGS_MAILDIR_READ = -lutil

SRCS_MAILDIR_READ_LETTER = header.c imsg-util.c leak.c maildir-read-letter.c
OBJS_MAILDIR_READ_LETTER = $(SRCS_MAILDIR_READ_LETTER:.c=.o)
LDFLAGS_MAILDIR_READ_LETTER = -lutil

SRCS = ${SRCS_MAILZ} ${SRCS_MAILDIR_READ} ${SRCS_MAILDIR_READ_LETTER}
OBJS = ${OBJS_MAILZ} ${OBJS_MAILDIR_READ} ${OBJS_MAILDIR_READ_LETTER}
DEPS = $(SRCS:.c=.d)

.PHONY: all clean install

all: maildir-read maildir-read-letter mailz

clean:
	rm -f ${OBJS} ${DEPS} lex.c maildir-read maildir-read-letter mailz parse.c parse.h

install:
	$(INSTALL) -m 0755 mailz ${PREFIX}/bin/
	$(INSTALL) -m 0755 -d ${PREFIX}/libexec/mailz
	$(INSTALL) -m 0755 maildir-read ${PREFIX}/libexec/mailz/
	$(INSTALL) -m 0755 maildir-read-letter ${PREFIX}/libexec/mailz/

maildir-read: ${OBJS_MAILDIR_READ}
	$(CC) -o $@ ${LDFLAGS_MAILDIR_READ} ${OBJS_MAILDIR_READ}

maildir-read-letter: ${OBJS_MAILDIR_READ_LETTER}
	$(CC) -o $@ ${LDFLAGS_MAILDIR_READ_LETTER} ${OBJS_MAILDIR_READ_LETTER}

mailz: ${OBJS_MAILZ}
	$(CC) -o $@ ${LDFLAGS_MAILZ} ${OBJS_MAILZ}

parse.c parse.h: parse.y
	$(YACC) -do parse.c ${.CURDIR}/parse.y

lex.c: lex.l
	$(LEX) -o lex.c ${.CURDIR}/lex.l

lex.c: parse.h

-include ${DEPS}
