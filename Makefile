DEBUG = -g -fno-inline-functions
CFLAGS = -O2 -pipe
CFLAGS += -MD -MP 
CFLAGS += -I${.CURDIR}/ 
CFLAGS += ${DEBUG}

PREFIX = /usr/local

CFLAGS += -Wall -Wpointer-arith -Wuninitialized -Wstrict-prototypes
CFLAGS += -Wmissing-prototypes -Wunused -Wsign-compare
CFLAGS += -Wshadow -Wdeclaration-after-statement

PREFIX = /usr/local

.PHONY: all clean install

all: maildir-read maildir-read-letter mailz

clean: clean-maildir-read clean-maildir-read-letter clean-mailz

install: install-maildir-read install-maildir-read-letter install-mailz

include mk/maildir-read.mk
include mk/maildir-read-letter.mk
include mk/mailz.mk
