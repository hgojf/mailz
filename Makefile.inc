.PATH: ${.CURDIR}/..

DEBUG = -g -fno-inline-functions

CFLAGS += -I${.CURDIR}/..
#CFLAGS += -Wsign-conversion
WARNINGS = Yes

TIDYCHECKS ?= \
	-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

PREFIX ?= /usr/local
BINDIR ?= ${PREFIX}/bin
LIBEXECDIR = ${PREFIX}/libexec

.PHONY: tidy

SRCS_TIDY = $(SRCS:%.l=${.OBJDIR}/%.c)
SRCS_TIDY := $(SRCS_TIDY:%.y=${.OBJDIR}/%.c)

tidy:
	cd ${.CURDIR}/.. && clang-tidy -checks=${TIDYCHECKS} ${SRCS_TIDY} -- ${CFLAGS}
