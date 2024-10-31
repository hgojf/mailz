.PHONY: all clean tidy

DEBUG = -g -fno-inline-functions

CFLAGS += ${DEBUG}
CFLAGS += -MD -MP
CFLAGS += -Wmissing-prototypes
CFLAGS += -Wall -Wextra

TIDYFLAGS += -checks=-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

all: mailz mailz-content
clean: clean-content clean-mailz
tidy: tidy-content tidy-mailz

include mk/content
include mk/mailz
