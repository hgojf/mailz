.PHONY: all clean test

.CURDIR ?= .
.OBJDIR ?= .

CFLAGS += -MD -MP

all: maildir-extract maildir-read-letter mailz regress

clean: maildir-extract-clean maildir-read-letter-clean mailz-clean regress-clean

test: maildir-extract maildir-read-letter regress
	@cd ${.CURDIR} && ${.OBJDIR}/regress

include mk/mailz
include mk/maildir-extract
include mk/maildir-read-letter
include mk/regress
