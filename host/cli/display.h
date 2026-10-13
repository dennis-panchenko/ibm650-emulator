/* Text rendering shared by the REPL and batch mode: words, disassembly,
 * trace lines with the timing bar, the console panel, the drum map. */
#ifndef I650_DISPLAY_H
#define I650_DISPLAY_H

#include <stdio.h>

#include "types.h"

#define DISPLAY_BAR_MAX 60

/* "RAU 0001 0055", or "op 25 0001 0055" for an unassigned code. */
void display_disassemble(struct i650_word w, char out[24]);

/* Word times as revolutions: "412.34" (one word time is 0.02 rev). */
void display_revs(uint64_t word_times, char out[24]);

/* The timing bar for one step, one character per word time:
 * '.' waiting for the drum, 'I' instruction read, 'D' data, '=' busy.
 * Writes up to max characters plus a NUL; returns the full length. */
size_t display_bar(const struct i650_step_record *r, char *out, size_t max);

/* The part of a trace line before the bar ("    0.96  0048  RAU 0001 0055  "),
 * with the time taken relative to the counters' epoch. */
void display_trace_prefix(const struct I650 *m, const struct i650_step_record *r,
                          char *out, size_t size);

/* Column headings for trace lines (the bar's legend included). */
void display_trace_header(FILE *f, bool bar);

/* A whole trace line, bar optional. */
void display_trace_line(FILE *f, const struct I650 *m, const struct i650_step_record *r,
                        bool bar);

/* "1520 instr · 412.34 rev (1.979 s) · idle 270.12 rev (65.5%)" */
void display_counters(const struct I650 *m, char *out, size_t size);

/* The console lights; biquinary adds each digit's seven lamps. */
void display_panel(FILE *f, const struct I650 *m, bool biquinary);

/* One memory line: "0048  6000010055+  RAU 0001 0055". */
void display_memory_line(FILE *f, const struct I650 *m, uint16_t address,
                         bool breakpoint);

/* The drum map: one row per band, one column per drum position. Shows
 * bands with anything in them, or only the band holding `only` if it is
 * a drum address. */
void display_map(FILE *f, const struct I650 *m, int only);

/* The 533's state on one line: "hopper 2 · feed 3 · read stacker 4 ·
 * punched 1", with "· END OF FILE" while that light is on (until the
 * last card has been read). */
void display_533(const struct I650 *m, char *out, size_t size);

/* The last n cards of a deck, numbered from 1, under a column ruler. */
void display_cards(FILE *f, const struct i650_deck *deck, size_t n);

#endif
