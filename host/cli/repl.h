#ifndef I650_REPL_H
#define I650_REPL_H

#include "types.h"

/* The interactive console; path (may be NULL) is loaded first. Reads
 * commands from stdin until q or end of input. */
int repl_run(struct I650 *m, const char *path);

#endif
