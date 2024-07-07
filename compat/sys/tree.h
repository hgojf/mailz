#ifndef MAILZ_COMPAT_SYS_TREE_H
#define MAILZ_COMPAT_SYS_TREE_H
#ifdef HAVE_SYS_TREE_H
#include_next <sys/tree.h>
#else
#include "bsd-tree.h"
#endif /* HAVE_SYS_TREE_H */
#endif /* MAILZ_COMPAT_SYS_TREE_H */
