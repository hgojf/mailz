#ifndef MAILZ_COMMANDS_H
#define MAILZ_COMMANDS_H
#include "conf.h"
#include "letter.h"

int commands_run(int, struct letter *, size_t, struct mailz_conf *);
#endif /* MAILZ_COMMANDS_H */
