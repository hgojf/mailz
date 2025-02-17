.PHONY: all clean tidy install

#DEBUG = -g -fno-inline-functions
CFLAGS += ${DEBUG}

CFLAGS += -MD -MP
CFLAGS += -Wmissing-prototypes -Wvla
CFLAGS += -Wall -Wextra
CFLAGS += -DPREFIX=\"${PREFIX}\"
CFLAGS += -I${.CURDIR}

PREFIX ?= /usr/local

TIDYFLAGS += -checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

all: mailz mailz-content
clean: clean-content clean-mailz
install: install-content install-mailz install-man
tidy: tidy-content tidy-mailz

include mk/content
include mk/mailz
include mk/man
