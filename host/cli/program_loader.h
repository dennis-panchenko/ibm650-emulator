/* Read a .650 file, parse it, load it; read and write deck files --
 * shared by batch mode and the REPL. Errors go to stderr as
 * "file:line:column: message". */
#ifndef I650_PROGRAM_LOADER_H
#define I650_PROGRAM_LOADER_H

#include "types.h"

bool program_loader_load(struct I650 *m, const char *path);

/* Writes the self-loading deck of a .650 file (see selfload.h); the
 * card count goes to *cards. */
bool program_loader_deck(const char *path, const char *deck_path, size_t *cards);

/* Reads a deck file into *deck (appending). Errors go to stderr as
 * "file:line: message". */
bool deck_loader_read(const char *path, struct i650_deck *deck);

/* Writes a deck file, one card per line. */
bool deck_loader_save(const char *path, const struct i650_deck *deck);

#endif
