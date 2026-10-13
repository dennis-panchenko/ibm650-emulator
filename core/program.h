/* The .650 program file: a drum image written by hand, one word per line
 * with its drum address always spelled out -- placement is the point.
 *
 *     ; comment (also after any line)
 *     start 0048                  optional: address register after load
 *     0048  RAU 0001 0055         instruction: mnemonic or 2-digit op, D, I
 *     0055  10 0008 0013
 *     0001  25+                   word: 1-10 digits, sign optional (+)
 *     0002  6000010055-
 *
 * Without `start` the address register is left at 8000 after loading, so
 * Program Start runs whatever is in the storage-entry switches -- the
 * real machine's way. */
#ifndef I650_PROGRAM_H
#define I650_PROGRAM_H

#include "types.h"

struct i650_program {
    struct i650_word words[I650_DRUM_WORDS];
    bool present[I650_DRUM_WORDS];
    unsigned line_of[I650_DRUM_WORDS];  /* source line, for duplicates */
    bool has_start;
    uint16_t start;
};

struct i650_program_error {
    unsigned line;      /* 1-based */
    unsigned column;    /* 1-based */
    char message[96];
};

/* Parses NUL-terminated source. On failure fills *err with the first
 * problem and returns false. */
bool i650_program_parse(const char *text, struct i650_program *out,
                        struct i650_program_error *err);

/* A word as written in a program file: 1-10 digits and an optional
 * trailing sign ("25+", "6000010055-", "7"). */
bool i650_program_parse_word(const char *text, struct i650_word *out);

/* Clears the drum, writes the program, does a computer reset, zeroes the
 * counters and sets the address register to the start address (8000 if
 * none). Console switches are left as they are. */
void i650_program_load(struct I650 *m, const struct i650_program *p);

#endif
