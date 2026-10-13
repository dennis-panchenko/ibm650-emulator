#include "accum.h"

#define HALF I650_WORD_LIMIT

void accum_clear(struct i650_accum *a) {
    a->upper = 0;
    a->lower = 0;
    a->neg = false;
    a->remainder_sign_valid = false;
    a->remainder_neg = false;
}

/* (u1:l1) >= (u2:l2) as 20-digit magnitudes. */
static bool mag_ge(uint64_t u1, uint64_t l1, uint64_t u2, uint64_t l2) {
    return u1 != u2 ? u1 > u2 : l1 >= l2;
}

/* Magnitude addition; returns the carry out of the upper half. */
static bool mag_add(uint64_t *u, uint64_t *l, uint64_t au, uint64_t al) {
    *l += al;
    *u += au + *l / HALF;
    *l %= HALF;
    bool carry = *u >= HALF;
    *u %= HALF;
    return carry;
}

/* (u:l) -= (su:sl); caller guarantees no borrow out. */
static void mag_sub(uint64_t *u, uint64_t *l, uint64_t su, uint64_t sl) {
    if (*l < sl) {
        *l += HALF;
        *u -= 1;
    }
    *l -= sl;
    *u -= su;
}

bool accum_add(struct i650_accum *a, uint64_t upper, uint64_t lower, bool neg,
               bool *complemented) {
    *complemented = false;
    if (neg == a->neg) {
        return mag_add(&a->upper, &a->lower, upper, lower);
    }
    if (mag_ge(a->upper, a->lower, upper, lower)) {
        mag_sub(&a->upper, &a->lower, upper, lower);
    } else {
        uint64_t u = upper, l = lower;
        mag_sub(&u, &l, a->upper, a->lower);
        a->upper = u;
        a->lower = l;
        a->neg = neg;
        *complemented = true;
    }
    return false;
}

unsigned accum_multiply(struct i650_accum *a, uint64_t multiplicand, bool multiplicand_neg) {
    uint64_t u = a->upper, l = a->lower;
    unsigned additions = 0;
    for (int i = 0; i < 10; i++) {
        unsigned counter = (unsigned)(u / (HALF / 10));
        u = (u % (HALF / 10)) * 10 + l / (HALF / 10);
        l = (l % (HALF / 10)) * 10;
        while (counter > 0) {
            if (mag_add(&u, &l, 0, multiplicand)) {
                counter++;
            }
            counter--;
            additions++;
        }
    }
    bool multiplier_neg = a->neg;
    a->upper = u;
    a->lower = l;
    a->neg = multiplier_neg != multiplicand_neg;
    /* p.8: a multiply after a DIV (no reset between) leaves the multiplier's
     * sign in the remainder-sign position, and STU stores that. */
    if (a->remainder_sign_valid) {
        a->remainder_neg = multiplier_neg;
    }
    return additions;
}

bool accum_divide(struct i650_accum *a, uint64_t divisor, bool divisor_neg,
                  bool keep_remainder, unsigned *quotient_digit_sum) {
    if (divisor == 0 || a->upper >= divisor) {
        return false;
    }
    /* Ten subtract-until-overdraw / shift cycles (Fig.15) == long division
     * of the 20-digit dividend, one lower-half digit at a time. */
    uint64_t r = a->upper, q = 0;
    unsigned sum = 0;
    for (unsigned pos = 10; pos >= 1; pos--) {
        r = r * 10 + i650_digit(a->lower, pos);
        unsigned digit = (unsigned)(r / divisor);
        r %= divisor;
        q = q * 10 + digit;
        sum += digit;
    }
    bool dividend_neg = a->neg;
    a->lower = q;
    a->neg = dividend_neg != divisor_neg;
    if (keep_remainder) {
        a->upper = r;
        a->remainder_sign_valid = true;
        a->remainder_neg = dividend_neg;
    } else {
        a->upper = 0;
        a->remainder_sign_valid = false;
    }
    *quotient_digit_sum = sum;
    return true;
}

/* digits[0] is position 20 (high-order of upper), digits[19] position 1. */
static void to_digits(const struct i650_accum *a, unsigned digits[20]) {
    uint64_t u = a->upper, l = a->lower;
    for (int i = 19; i >= 10; i--) {
        digits[i] = (unsigned)(l % 10);
        l /= 10;
    }
    for (int i = 9; i >= 0; i--) {
        digits[i] = (unsigned)(u % 10);
        u /= 10;
    }
}

static void from_digits(struct i650_accum *a, const unsigned digits[20]) {
    uint64_t u = 0, l = 0;
    for (int i = 0; i < 10; i++) {
        u = u * 10 + digits[i];
    }
    for (int i = 10; i < 20; i++) {
        l = l * 10 + digits[i];
    }
    a->upper = u;
    a->lower = l;
}

/* Shift by n (positive = left), zero fill. Returns the last digit shifted
 * out on the right (0 if none) for SRD's rounding. */
static unsigned shift(struct i650_accum *a, int n) {
    unsigned d[20], out[20] = { 0 }, last_out = 0;
    to_digits(a, d);
    for (int i = 0; i < 20; i++) {
        int src = i + n;
        if (src >= 0 && src < 20) {
            out[i] = d[src];
        }
    }
    if (n < 0 && -n <= 20) {
        last_out = d[20 + n];
    }
    from_digits(a, out);
    return last_out;
}

void accum_shift_right(struct i650_accum *a, unsigned n) {
    shift(a, -(int)n);
}

void accum_shift_left(struct i650_accum *a, unsigned n) {
    shift(a, (int)n);
}

void accum_shift_right_round(struct i650_accum *a, unsigned n) {
    if (shift(a, -(int)n) >= 5) {
        mag_add(&a->upper, &a->lower, 0, 1);
    }
}

bool accum_shift_left_count(struct i650_accum *a, unsigned units_digit, unsigned *shifts_taken) {
    unsigned max = units_digit == 0 ? 10 : units_digit;
    unsigned shifts = 0;
    while (shifts < max && a->upper / (HALF / 10) == 0) {
        shift(a, 1);
        shifts++;
    }
    bool overflow = a->upper / (HALF / 10) == 0;
    unsigned count;
    if (overflow) {
        count = 10;
    } else if (shifts == 0) {
        count = 0;      /* "replaced by zeros regardless of the units position" */
    } else if (units_digit == 0) {
        count = shifts;
    } else {
        count = (10 - units_digit) + shifts;
    }
    a->lower = i650_set_field(a->lower, 2, 1, count);
    *shifts_taken = shifts;
    return overflow;
}
