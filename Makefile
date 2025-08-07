.PHONY: all

all: mailz mailz-content regress-run

CFLAGS += -isystem compat/
CFLAGS += -D_DEFAULT_SOURCE -D_GNU_SOURCE
CFLAGS += -D"pledge(a,b)=0" -D"unveil(a,b)=0"
CFLAGS += -isystem /usr/include/bsd -DLIBBSD_OVERLAY
LDFLAGS += -lbsd
SRCS_IMSG = compat/imsg.c compat/imsg-buffer.c
INSTALL ?= install

# Uncomment for better debugger use
#CFLAGS += -g -O0

# Generate dependency files
CFLAGS += -MD -MP

CFLAGS += -Wall -Wextra
CFLAGS += -Wmissing-prototypes -Wvla

CFLAGS += -std=c99 -pedantic

# This is needed for outputting object files to the subdirectory
# where their source file is located, as POSIX does not include the
# -o $@.
.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

CFLAGS += -DPREFIX=\"$(PREFIX)\"

LDFLAGS_CONTENT = -lutil
SRCS_CONTENT = charset.c content.c encoding.c header.c imsg-blocking.c
SRCS_CONTENT += compat/base64.c $(SRCS_IMSG)

DEPS_CONTENT = $(SRCS_CONTENT:.c=.d)
OBJS_CONTENT = $(SRCS_CONTENT:.c=.o)

mailz-content: $(OBJS_CONTENT)
	$(CC) -o $@ $(LDFLAGS) $(LDFLAGS_CONTENT) $(OBJS_CONTENT)

-include $(DEPS_CONTENT)

LDFLAGS_MAILZ = -lutil
SRCS_MAILZ = content-proc.c err-fork.c imsg-blocking.c lex.c mailbox.c
SRCS_MAILZ += maildir.c mailz.c parse.c printable.c
SRCS_MAILZ += $(SRCS_IMSG)

DEPS_MAILZ = $(SRCS_MAILZ:.c=.d)
OBJS_MAILZ = $(SRCS_MAILZ:.c=.o)

parse.c parse.h: parse.y
	$(YACC) -d -o parse.c parse.y
lex.c: lex.l
lex.o: parse.h

mailz: $(OBJS_MAILZ)
	$(CC) -o $@ $(LDFLAGS) $(LDFLAGS_MAILZ) $(OBJS_MAILZ)

-include $(DEPS_MAILZ)

LDFLAGS_REGRESS = -lutil
SRCS_REGRESS = err-fork.c charset.c content-proc.c encoding.c header.c
SRCS_REGRESS += imsg-blocking.c mailbox.c maildir.c printable.c
SRCS_REGRESS += regress/charset.c regress/content-proc.c
SRCS_REGRESS += regress/encoding.c regress/header.c regress/mailbox.c
SRCS_REGRESS += regress/maildir.c regress/printable.c regress/regress.c
SRCS_REGRESS += compat/base64.c $(SRCS_IMSG)

DEPS_REGRESS = $(SRCS_REGRESS:.c=.d)
OBJS_REGRESS = $(SRCS_REGRESS:.c=.o)

regress-run: $(OBJS_REGRESS)
	$(CC) $(LDFLAGS) -o $@ $(LDFLAGS_REGRESS) $(OBJS_REGRESS)
.PHONY: test

test: mailz-content regress-run
	@./regress-run

-include $(DEPS_REGRESS)

SRCS_ALL = err-fork.c charset.c content-proc.c content.c encoding.c
SRCS_ALL += header.c imsg-blocking.c mailbox.c maildir.c mailz.c printable.c
SRCS_ALL += regress/charset.c regress/content-proc.c regress/encoding.c
SRCS_ALL += regress/header.c regress/mailbox.c regress/maildir.c
SRCS_ALL +=  regress/printable.c regress/regress.c
SRCS_ALL += compat/base64.c compat/imsg-buffer.c
SRCS_ALL += compat/imsg.c
SRCS_GENERATED = lex.c parse.c

.PHONY: tidy

TIDYCHECKS = -clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-clang-analyzer-unix.Stream
TIDYFLAGS = -checks=$(TIDYCHECKS)
tidy:
	clang-tidy $(TIDYFLAGS) $(SRCS_ALL) -- $(CFLAGS)

.PHONY: clean

SRCS_REAL = $(SRCS_ALL) $(SRCS_GENERATED)

BINARIES = mailz mailz-content regress-run
DEPS_REAL = $(SRCS_REAL:.c=.d)
OBJS_REAL = $(SRCS_REAL:.c=.o)

clean:
	rm -f $(BINARIES) $(DEPS_REAL) $(OBJS_REAL) $(SRCS_GENERATED) tags parse.h

HEADERS = err-fork.h charset.h conf.h content-proc.h content.h header.h
HEADERS += imsg-blocking.h mailbox.h maildir.h regress/charset.h
HEADERS += regress/content-proc.h regress/encoding.h regress/header.h
HEADERS += regress/mailbox.h regress/maildir.h regress/printable.h

tags: $(SRCS_ALL) $(HEADERS)
	$(CTAGS) -f $@ $(SRCS_ALL) $(HEADERS)

.PHONY: install

PREFIX ?= /usr/local
MANPATH ?= $(PREFIX)/man

install:
	$(INSTALL) -m 0755 mailz $(PREFIX)/bin
	$(INSTALL) -m 0755 mailz-content $(PREFIX)/libexec
	$(INSTALL) -m 0644 mailz.1 ${MANPATH}/man1/
	$(INSTALL) -m 0644 mailz.conf.5 ${MANPATH}/man5/
