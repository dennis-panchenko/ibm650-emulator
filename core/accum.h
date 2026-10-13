/* Accumulator arithmetic: the 20-digit adder, multiply/divide loops and
 * shifts. Pure functions on struct i650_accum -- no drum, no timing --
 * so each can be checked directly against the BasicOpcodes figures.
 * cpu.c decides which one an op code calls and what it costs. */
#ifndef I650_ACCUM_H
#define I650_ACCUM_H

#include "types.h"

void accum_clear(struct i650_accum *a);

/* Adds a signed 20-digit value (upper:lower halves of the addend) to the
 * accumulator. Sign rules are BasicOpcodes p.1 Fig.2: like signs add, unlike
 * signs subtract and the result takes the sign of the larger. A zero result
 * keeps the accumulator's sign. *complemented is set when the result had to
 * be recomplemented (sign changed), which costs two extra word times.
 * Returns true on overflow (carry out of the upper half; the carry is lost). */
bool accum_add(struct i650_accum *a, uint64_t upper, uint64_t lower, bool neg,
               bool *complemented);

/* 19 MPY: multiplier in the upper half, multiplicand magnitude given.
 * Runs the machine's own loop (Fig.11): shift left, then add the
 * multiplicand into the lower half once per unit of the digit shifted out.
 * A carry out of the top during an add lands in that digit counter and
 * buys one extra add -- which reproduces Fig.12C's "incorrect answer"
 * exactly. Returns the number of additions (the timing chart's sum of
 * multiplier digits). */
unsigned accum_multiply(struct i650_accum *a, uint64_t multiplicand, bool multiplicand_neg);

/* 14 DIV / 64 DVR. Returns false (accumulator untouched) on quotient
 * overflow: divisor zero or |divisor| <= |upper half| (p.7). Otherwise the
 * quotient goes to the lower half with the sign-analysis sign; for DIV the
 * remainder stays in the upper half with the dividend's sign, for DVR the
 * upper half is cleared. *quotient_digit_sum feeds the timing chart. */
bool accum_divide(struct i650_accum *a, uint64_t divisor, bool divisor_neg,
                  bool keep_remainder, unsigned *quotient_digit_sum);

/* Shifts never change the sign(s) (p.11). n is 0..10 positions. */
void accum_shift_right(struct i650_accum *a, unsigned n);
void accum_shift_left(struct i650_accum *a, unsigned n);
/* 31 SRD: shift right n (1..10) and add 5 to the last digit shifted out. */
void accum_shift_right_round(struct i650_accum *a, unsigned n);
/* 36 SCT: shift left until the high-order digit is significant, at most
 * 10 places if units_digit is 0, else at most units_digit places; then put
 * the count in the two low-order positions (p.12). Returns true on
 * overflow (still no significant digit at the maximum). *shifts_taken is
 * the number of positions actually shifted. */
bool accum_shift_left_count(struct i650_accum *a, unsigned units_digit, unsigned *shifts_taken);

#endif
