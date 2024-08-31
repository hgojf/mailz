.PATH: ${.CURDIR}/..

DEBUG = -g -fno-inline-functions

CFLAGS += -I${.CURDIR}/..
#CFLAGS += -Wsign-conversion
WARNINGS = Yes

CFLOW ?= cflow
SRCS_CFLOW = $(SRCS:M*.c)

TIDYCHECKS ?= \
	-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

PREFIX ?= /usr/local/

realinstall:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} \
		-m ${BINMODE} ${PROG} ${DESTDIR}${PREFIX}${BINDIR}/${PROG}

.PHONY: cflow tidy

cflow:
	cd ${.CURDIR}/.. && $(CFLOW) -- $(SRCS_CFLOW)

SRCS_TIDY = $(SRCS:%.l=${.OBJDIR}/%.c)
SRCS_TIDY := $(SRCS_TIDY:%.y=${.OBJDIR}/%.c)

tidy:
	cd ${.CURDIR}/.. && clang-tidy -checks=${TIDYCHECKS} ${SRCS_TIDY} -- ${CFLAGS}
