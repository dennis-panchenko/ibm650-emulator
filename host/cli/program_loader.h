/* Read a .650 file, parse it, load it -- shared by batch mode and the
 * REPL. Errors go to stderr as "file:line:column: message". */
#ifndef I650_PROGRAM_LOADER_H
#define I650_PROGRAM_LOADER_H

#include "types.h"

bool program_loader_load(struct I650 *m, const char *path);

#endif
