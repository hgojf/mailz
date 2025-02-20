.PHONY: all clean tidy install

#DEBUG = -g -fno-inline-functions
CFLAGS += ${DEBUG}

CFLAGS += -MD -MP
CFLAGS += -Wmissing-prototypes -Wvla
CFLAGS += -Wall -Wextra
CFLAGS += -DPREFIX=\"${PREFIX}\"

# This is needed for outputting object files to the subdirectory
# where their source file is located, as POSIX does not include the
# -o $@.
.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

PREFIX ?= /usr/local

TIDYFLAGS += -checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

all: mailz mailz-content regress-run
clean: clean-content clean-mailz clean-regress 
install: install-content install-mailz install-man
tidy: tidy-content tidy-mailz tidy-regress

include mk/content
include mk/mailz
include mk/man
include mk/regress
