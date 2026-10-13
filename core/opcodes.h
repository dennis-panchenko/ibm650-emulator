/* Operation-code mnemonics: the SOAP II names, which the later IBM
 * manuals use too. */
#ifndef I650_OPCODES_H
#define I650_OPCODES_H

#include <stdbool.h>

/* The mnemonic for op (00-99), or NULL if a basic 650 has no such
 * operation. */
const char *i650_mnemonic(unsigned op);

/* The op code for a mnemonic (case-insensitive), or -1. */
int i650_opcode(const char *mnemonic);

#endif
