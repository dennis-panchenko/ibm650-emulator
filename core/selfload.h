/* A .650 program as a self-loading deck: the one-instruction-per-card
 * load routine of Andree, Programming the IBM 650 (1958), section 2.6.
 * Every card carries its own loader:
 *
 *     word 1 (cols  1-10)   69 1954 1953   LDD the card's word 4, go to 1953
 *     word 2 (cols 11-20)   blank          (problem reference, if wanted)
 *     word 3 (cols 21-30)   24 AAAA 8000   STD to drum address AAAA, go to 8000
 *     word 4 (cols 31-40)   the word to store
 *     words 5-8             blank          (comments, if wanted)
 *
 * with a 12 (load) punch in column 1. The storage-entry switches hold
 * 70 1951 9000: RD a card into 1951-1960, and being a load card it
 * branches to 1951; 1953 goes back to the switches for the next card. A
 * final transfer card, 00 0000 SSSS, sends the machine to the program's
 * start. A non-load card would go to 9000 -- a storage selection stop.
 *
 * The read area 1951-1960 is overwritten by every card, so a program
 * can't have words there. */
#ifndef I650_SELFLOAD_H
#define I650_SELFLOAD_H

#include "program.h"

/* The storage-entry switch setting that boots a self-loading deck. */
#define I650_SELFLOAD_ENTRY 7019519000ull
#define I650_SELFLOAD_FIRST 1951u     /* the read area the loader uses */
#define I650_SELFLOAD_LAST 1960u

enum i650_selfload {
    I650_SELFLOAD_OK,
    I650_SELFLOAD_NO_START,     /* the program has no `start` line */
    I650_SELFLOAD_READ_AREA,    /* a word in 1951-1960: *at is its address */
    I650_SELFLOAD_NO_MEMORY,
};

/* Appends the deck to *out: one card per word, in address order, then
 * the transfer card. */
enum i650_selfload i650_program_deck(const struct i650_program *p, struct i650_deck *out,
                                     uint16_t *at);

#endif
