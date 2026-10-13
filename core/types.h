/* Shared machine types. No platform headers here -- this must compile
 * cleanly for every host that links against the core. */
#ifndef I650_TYPES_H
#define I650_TYPES_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define I650_DRUM_WORDS 2000u
#define I650_WORDS_PER_BAND 50u     /* one band = one revolution's worth */
#define I650_WORD_LIMIT 10000000000ull  /* 10^10: a word holds 10 digits */

/* The four addressable non-drum locations (GeneralInfo p.11: "Valid
 * addresses are 0000-1999, 8000, 8001, 8002, 8003"). */
#define I650_ADDR_SWITCHES 8000u    /* console storage-entry switches */
#define I650_ADDR_DISTRIBUTOR 8001u
#define I650_ADDR_LOWER 8002u       /* lower accumulator */
#define I650_ADDR_UPPER 8003u       /* upper accumulator */

/* --- Word ---
 * 10 decimal digits plus a sign (GeneralInfo p.4). The hardware encodes
 * each digit in bi-quinary; that only matters for display, so the core
 * keeps the magnitude as a plain integer (see STATE.md "Deliberately
 * simplified"). Sign-magnitude, so minus zero exists: BasicOpcodes p.15
 * notes "it is possible for the accumulator to be both zero and minus". */
struct i650_word {
    uint64_t mag;   /* 0 .. 10^10 - 1 */
    bool neg;
};

static inline struct i650_word i650_word_make(uint64_t mag, bool neg) {
    struct i650_word w = { mag % I650_WORD_LIMIT, neg };
    return w;
}

static inline uint64_t i650_pow10(unsigned n) {
    uint64_t p = 1;
    while (n-- > 0) {
        p *= 10;
    }
    return p;
}

/* Digit positions are numbered the way IBM does: 1 = units (rightmost),
 * 10 = high-order. Returns the field of positions hi..lo as a number. */
static inline uint64_t i650_field(uint64_t mag, unsigned hi, unsigned lo) {
    return (mag / i650_pow10(lo - 1)) % i650_pow10(hi - lo + 1);
}

static inline uint64_t i650_set_field(uint64_t mag, unsigned hi, unsigned lo,
                                      uint64_t value) {
    uint64_t unit = i650_pow10(lo - 1);
    uint64_t width = i650_pow10(hi - lo + 1);
    uint64_t old = (mag / unit) % width;
    return mag - old * unit + (value % width) * unit;
}

static inline unsigned i650_digit(uint64_t mag, unsigned position) {
    return (unsigned)i650_field(mag, position, position);
}

/* --- Instruction format ---
 * op(2) D-address(4) I-address(4), sign ignored (GeneralInfo p.6-7):
 * positions 10-9 op, 8-5 data address, 4-1 next-instruction address. */
static inline unsigned i650_op(struct i650_word w) {
    return (unsigned)i650_field(w.mag, 10, 9);
}

static inline uint16_t i650_d_address(struct i650_word w) {
    return (uint16_t)i650_field(w.mag, 8, 5);
}

static inline uint16_t i650_i_address(struct i650_word w) {
    return (uint16_t)i650_field(w.mag, 4, 1);
}

static inline struct i650_word i650_instruction(unsigned op, uint16_t d, uint16_t i) {
    return i650_word_make((uint64_t)op * 100000000ull + (uint64_t)d * 10000ull + i, false);
}

static inline bool i650_valid_address(uint16_t a) {
    return a < I650_DRUM_WORDS || (a >= I650_ADDR_SWITCHES && a <= I650_ADDR_UPPER);
}

/* "0123456789+" / "0123456789-": the way the manuals write a word. */
static inline void i650_format_word(struct i650_word w, char out[12]) {
    uint64_t m = w.mag;
    for (int i = 9; i >= 0; i--) {
        out[i] = (char)('0' + m % 10);
        m /= 10;
    }
    out[10] = w.neg ? '-' : '+';
    out[11] = '\0';
}

/* Parses exactly 10 digits followed by '+' or '-' (the format above). */
static inline bool i650_parse_word(const char *text, struct i650_word *out) {
    uint64_t m = 0;
    for (int i = 0; i < 10; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        m = m * 10 + (uint64_t)(text[i] - '0');
    }
    if (text[10] != '+' && text[10] != '-') {
        return false;
    }
    *out = i650_word_make(m, text[10] == '-');
    return true;
}

/* --- Accumulator ---
 * 20 digits with one sign, split into upper (8003) and lower (8002)
 * halves. Arithmetic treats it as one 20-digit sign-magnitude number:
 * a carry out of the lower half goes into the upper (BasicOpcodes p.2),
 * a carry out of the upper half is an overflow (p.3). */
struct i650_accum {
    uint64_t upper;     /* 0 .. 10^10 - 1 */
    uint64_t lower;     /* 0 .. 10^10 - 1 */
    bool neg;

    /* After 14 DIV the upper half (the remainder) keeps its own sign --
     * the only time the halves differ -- until the next reset, multiply
     * or divide (BasicOpcodes p.7). 21 STU stores this sign while it's
     * valid. */
    bool remainder_sign_valid;
    bool remainder_neg;
};

enum i650_status {
    I650_RUNNING,
    I650_HALTED,                    /* 01 HLT with Programmed switch on STOP */
    I650_STOP_OVERFLOW,             /* overflow with Overflow switch on STOP */
    I650_STOP_DIVIDE,               /* quotient overflow / divide by zero: always stops */
    I650_STOP_STORAGE_SELECTION,    /* invalid address (incl. TLU running off 1999) */
    I650_STOP_INVALID_OP,           /* op code not assigned on a basic 650 */
    I650_STOP_DISTRIBUTOR_DIGIT,    /* 90-99 BD found neither 8 nor 9 */
    I650_STOP_NO_IO,                /* 70-78 read/write: card I/O not built yet */
};

struct I650 {
    struct i650_word drum[I650_DRUM_WORDS];
    struct i650_word storage_entry;     /* 8000: console switches */
    struct i650_word distributor;       /* 8001 */
    struct i650_accum acc;              /* 8002 / 8003 */
    bool overflow;                      /* overflow indicator (console light) */

    struct i650_word program_register;  /* instruction being executed */
    uint16_t instruction_address;       /* where it was read from */
    uint16_t address_register;          /* where the next one comes from */

    /* Console switches (GeneralInfo p.15). Physical switches, so a
     * computer reset leaves them alone. */
    bool programmed_stop;   /* Programmed: true = STOP (HLT halts), false = RUN */
    bool overflow_stop;     /* Overflow: true = STOP, false = SENSE (BOV can test it) */

    /* Timing, in word times (see drum.h). word_time is the elapsed clock;
     * the other two carry an instruction's timing over to the next one. */
    uint64_t word_time;         /* when the last instruction's work ended */
    uint64_t ready_at;          /* next instruction can't be read before this */
    uint64_t interlock_until;   /* ...nor start executing before this (shifts) */
    uint64_t last_fetch_time;   /* when program_register was read */

    enum i650_status status;
};

#endif
