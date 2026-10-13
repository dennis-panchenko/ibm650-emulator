#include "cpu.h"
#include "accum.h"
#include "drum.h"

#include <stdlib.h>

struct I650 *i650_create(void) {
    struct I650 *m = calloc(1, sizeof(*m));
    if (m == NULL) {
        return NULL;
    }
    m->programmed_stop = true;
    m->overflow_stop = false;
    i650_computer_reset(m);
    return m;
}

void i650_destroy(struct I650 *m) {
    free(m);
}

void i650_computer_reset(struct I650 *m) {
    accum_clear(&m->acc);
    m->distributor = i650_word_make(0, false);
    m->program_register = i650_word_make(0, false);
    m->instruction_address = I650_ADDR_SWITCHES;
    m->address_register = I650_ADDR_SWITCHES;
    m->overflow = false;
    m->ready_at = m->word_time;
    m->interlock_until = m->word_time;
    m->status = I650_RUNNING;
}

void i650_program_start(struct I650 *m) {
    if (i650_status_resumable(m->status)) {
        m->status = I650_RUNNING;
    }
}

/* --- Execution ---
 * Each op handler gets the instruction plus t, the word time at which it
 * starts executing (the chart's "n": normally when it was read from the
 * drum, later if a shift's interlock held it). Timing offsets below are
 * the Optimum Program Chart's (BasicOpcodes p.23): "data at n+3" means the
 * data address can be read no earlier than t+3, "instruction at d+5" means
 * the next instruction no earlier than 5 word times after the data was
 * read. EVEN/ODD columns refer to the parity of t (or of the data time),
 * which is the parity of the drum position because 50 is even. */

struct exec {
    struct I650 *m;
    unsigned op;
    uint16_t d;
    uint16_t i;
    uint64_t t;
    bool even;
};

/* Records the data access for the trace and idle counters. */
static void note_data(struct exec *x, uint16_t address, uint64_t ready, uint64_t at,
                      uint8_t touch) {
    struct I650 *m = x->m;
    m->last_step.has_data = true;
    m->last_step.data_address = address;
    m->last_step.data_ready = ready;
    m->last_step.data_time = at;
    m->counters.idle_data += at - ready;
    if (address < I650_DRUM_WORDS) {
        m->touched[address] |= touch;
    }
}

static bool is_even(uint64_t t) {
    return t % 2 == 0;
}

static void finish(struct exec *x, uint16_t next, uint64_t ready_at) {
    struct I650 *m = x->m;
    m->address_register = next;
    m->ready_at = ready_at;
    m->word_time = ready_at > m->interlock_until ? ready_at : m->interlock_until;
}

static void error_stop(struct exec *x, enum i650_status status, uint64_t at) {
    struct I650 *m = x->m;
    m->status = status;
    m->address_register = m->instruction_address;
    m->ready_at = at;
    m->interlock_until = at;
    m->word_time = at;
}

/* A verifiable overflow: lights the indicator for 47 BOV, and stops the
 * machine (after the instruction completes) if the Overflow switch is on
 * STOP (p.15). */
static void signal_overflow(struct exec *x) {
    x->m->overflow = true;
    if (x->m->overflow_stop) {
        x->m->status = I650_STOP_OVERFLOW;
    }
}

/* Every arithmetic op first moves its D-address contents to the
 * distributor (p.1); with D = 8001 the distributor is used as it stands. */
static uint64_t load_distributor(struct exec *x, uint64_t earliest) {
    uint64_t td = drum_wait_for(earliest, x->d);
    note_data(x, x->d, earliest, td, I650_TOUCH_READ);
    if (x->d != I650_ADDR_DISTRIBUTOR) {
        x->m->distributor = drum_read(x->m, x->d);
    }
    return td;
}

/* 10 11 15 16 17 18 (add into the accumulator as it is) and
 * 60 61 65 66 67 68 (reset to plus zero first). */
static void op_add(struct exec *x, bool reset, bool to_upper, bool subtract, bool magnitude) {
    struct I650 *m = x->m;
    uint64_t td = load_distributor(x, x->t + 3);
    struct i650_word v = m->distributor;
    bool neg = magnitude ? false : v.neg;
    if (subtract) {
        neg = !neg;
    }
    if (reset) {
        accum_clear(&m->acc);
    }
    bool complemented;
    if (accum_add(&m->acc, to_upper ? v.mag : 0, to_upper ? 0 : v.mag, neg, &complemented)) {
        signal_overflow(x);
    }
    /* Recomplementing costs 2 word times (p.26); reset ops start from zero
     * and aren't listed as taking that cycle. */
    uint64_t extra = (complemented && !reset) ? 2 : 0;
    finish(x, x->i, td + (is_even(td) ? 5 : 4) + extra);
}

static void op_multiply(struct exec *x) {
    struct I650 *m = x->m;
    uint64_t td = load_distributor(x, x->t + 3);
    unsigned additions = accum_multiply(&m->acc, m->distributor.mag, m->distributor.neg);
    finish(x, x->i, td + (is_even(td) ? 21 : 20) + 2 * (uint64_t)additions);
}

static void op_divide(struct exec *x, bool keep_remainder) {
    struct I650 *m = x->m;
    uint64_t td = load_distributor(x, x->t + 3);
    unsigned digit_sum;
    if (!accum_divide(&m->acc, m->distributor.mag, m->distributor.neg, keep_remainder, &digit_sum)) {
        m->overflow = true;
        error_stop(x, I650_STOP_DIVIDE, td);
        return;
    }
    finish(x, x->i, td + (is_even(td) ? 61 : 60) + 2 * (uint64_t)digit_sum);
}

/* 20-24: the value goes through the distributor on its way to the drum
 * (p.8). Only 0000-1999 is a valid store address. */
static void op_store(struct exec *x, unsigned even_offset, unsigned odd_offset,
                     struct i650_word value) {
    struct I650 *m = x->m;
    if (x->d >= I650_DRUM_WORDS) {
        error_stop(x, I650_STOP_STORAGE_SELECTION, x->t);
        return;
    }
    m->distributor = value;
    uint64_t ready = x->t + (x->even ? even_offset : odd_offset);
    uint64_t td = drum_wait_for(ready, x->d);
    note_data(x, x->d, ready, td, I650_TOUCH_WRITTEN);
    drum_write(m, x->d, value);
    finish(x, x->i, td + 3);
}

static void op_load_distributor(struct exec *x) {
    uint64_t td = load_distributor(x, x->t + 3);
    finish(x, x->i, td + 3);
}

/* Branches: "yes" goes to the D-address, "no" to the I-address (p.13).
 * Offsets are the chart's, by the parity of the instruction's own time. */
static void branch(struct exec *x, bool taken, unsigned d_even, unsigned d_odd,
                   unsigned i_even, unsigned i_odd) {
    unsigned offset = taken ? (x->even ? d_even : d_odd) : (x->even ? i_even : i_odd);
    finish(x, taken ? x->d : x->i, x->t + offset);
}

/* 90-99: 90 tests position 10, 91-99 positions 1-9 of the distributor. */
static void op_branch_digit(struct exec *x) {
    unsigned position = x->op == 90 ? 10 : x->op - 90;
    unsigned digit = i650_digit(x->m->distributor.mag, position);
    if (digit != 8 && digit != 9) {
        error_stop(x, I650_STOP_DISTRIBUTOR_DIGIT, x->t);
        return;
    }
    bool outer = x->op == 90 || x->op == 99;
    branch(x, digit == 8, outer ? 4 : 3, outer ? 4 : 3, 5, 5);
}

/* 30 SRT, 31 SRD, 35 SLT, 36 SCT. The shift amount is the units digit of
 * the D-address. The next instruction may be read once the shift is
 * under way (the chart's low limit) but can't start until it's done (the
 * high limit) -- modelled by interlock_until. */
static void op_shift(struct exec *x) {
    struct I650 *m = x->m;
    unsigned units = x->d % 10;
    unsigned shifts = units;
    switch (x->op) {
    case 30:
        accum_shift_right(&m->acc, units);
        break;
    case 35:
        accum_shift_left(&m->acc, units);
        break;
    case 31:
        shifts = units == 0 ? 10 : units;
        accum_shift_right_round(&m->acc, shifts);
        break;
    default:
        if (accum_shift_left_count(&m->acc, units, &shifts)) {
            signal_overflow(x);
        }
        break;
    }
    uint64_t low, high;
    if (x->op == 31) {
        low = x->even ? 7 : 6;
        high = 2 * (uint64_t)shifts + (x->even ? 5 : 4);
    } else if (shifts == 0) {
        low = high = x->even ? 6 : 5;
    } else {
        low = x->even ? 7 : 6;
        high = 2 * (uint64_t)shifts + (x->even ? 3 : 2);
    }
    if (high < low) {
        high = low;
    }
    m->interlock_until = x->t + high;
    finish(x, x->i, x->t + low);
}

/* 84 TLU (p.17-18): search from the first word of the D-address's band
 * for the first argument whose magnitude is >= the distributor's, skipping
 * the last two words of every band. D-address + locations passed goes to
 * positions 8-5 of the lower accumulator; nothing else changes. */
static void op_table_lookup(struct exec *x) {
    struct I650 *m = x->m;
    if (x->d >= I650_DRUM_WORDS) {
        error_stop(x, I650_STOP_STORAGE_SELECTION, x->t);
        return;
    }
    uint16_t band = (uint16_t)(x->d - x->d % I650_WORDS_PER_BAND);
    uint64_t t0 = drum_wait_for(x->t + 3, band);
    note_data(x, band, x->t + 3, t0, I650_TOUCH_READ);
    uint16_t a = band;
    for (;; a++) {
        if (a >= I650_DRUM_WORDS) {
            /* "the machine stops when it attempts to search location 2000" */
            error_stop(x, I650_STOP_STORAGE_SELECTION, t0 + (a - band));
            return;
        }
        if (a % I650_WORDS_PER_BAND < 48) {
            m->touched[a] |= I650_TOUCH_READ;
            if (m->drum[a].mag >= m->distributor.mag) {
                break;
            }
        }
    }
    uint64_t ta = t0 + (a - band);
    m->acc.lower = i650_set_field(m->acc.lower, 8, 5, (uint64_t)x->d + (a - band));
    finish(x, x->i, ta + (is_even(ta) ? 5 : 6));
}

static void dispatch(struct exec *x) {
    struct I650 *m = x->m;
    struct i650_accum *acc = &m->acc;
    switch (x->op) {
    /* Arithmetic: reset, upper, subtract, magnitude */
    case 10: op_add(x, false, true, false, false); break;   /* AUP */
    case 11: op_add(x, false, true, true, false); break;    /* SUP */
    case 15: op_add(x, false, false, false, false); break;  /* ALO */
    case 16: op_add(x, false, false, true, false); break;   /* SLO */
    case 17: op_add(x, false, false, false, true); break;   /* AML */
    case 18: op_add(x, false, false, true, true); break;    /* SML */
    case 60: op_add(x, true, true, false, false); break;    /* RAU */
    case 61: op_add(x, true, true, true, false); break;     /* RSU */
    case 65: op_add(x, true, false, false, false); break;   /* RAL */
    case 66: op_add(x, true, false, true, false); break;    /* RSL */
    case 67: op_add(x, true, false, false, true); break;    /* RAM */
    case 68: op_add(x, true, false, true, true); break;     /* RSM */
    case 19: op_multiply(x); break;                         /* MPY */
    case 14: op_divide(x, true); break;                     /* DIV */
    case 64: op_divide(x, false); break;                    /* DVR */

    /* Stores */
    case 20:                                                /* STL */
        op_store(x, 5, 4, i650_word_make(acc->lower, acc->neg));
        break;
    case 21:                                                /* STU */
        op_store(x, 4, 5, i650_word_make(acc->upper,
                 acc->remainder_sign_valid ? acc->remainder_neg : acc->neg));
        break;
    case 22:                                                /* SDA */
        op_store(x, 3, 4, i650_word_make(i650_set_field(m->distributor.mag, 8, 5,
                 i650_field(acc->lower, 8, 5)), m->distributor.neg));
        break;
    case 23:                                                /* SIA */
        op_store(x, 3, 4, i650_word_make(i650_set_field(m->distributor.mag, 4, 1,
                 i650_field(acc->lower, 4, 1)), m->distributor.neg));
        break;
    case 24: op_store(x, 3, 3, m->distributor); break;      /* STD */
    case 69: op_load_distributor(x); break;                 /* LDD */

    /* Shifts */
    case 30: case 31: case 35: case 36: op_shift(x); break;

    /* Branches */
    case 44:                                                /* NZU */
        branch(x, acc->upper != 0, 3, 4, 4, 5);
        break;
    case 45:                                                /* NZE */
        branch(x, acc->upper != 0 || acc->lower != 0, 4, 3, 5, 4);
        break;
    case 46: branch(x, acc->neg, 3, 3, 4, 4); break;        /* BMI */
    case 47: {                                              /* BOV */
        bool was = m->overflow;
        m->overflow = false;
        branch(x, was, 3, 3, 5, 5);
        break;
    }
    case 90: case 91: case 92: case 93: case 94:
    case 95: case 96: case 97: case 98: case 99:
        op_branch_digit(x);
        break;

    /* Miscellaneous */
    case 0: finish(x, x->i, x->t + 4); break;               /* NOP */
    case 1:                                                 /* HLT */
        if (m->programmed_stop) {
            m->status = I650_HALTED;
        }
        finish(x, x->i, x->t + 4);
        break;
    case 84: op_table_lookup(x); break;                     /* TLU */

    case 70: case 71: case 72: case 73: case 74:
    case 75: case 76: case 77: case 78:
        error_stop(x, I650_STOP_NO_IO, x->t);
        break;

    default:
        error_stop(x, I650_STOP_INVALID_OP, x->t);
        break;
    }
}

static bool known_op(unsigned op) {
    switch (op) {
    case 0: case 1: case 10: case 11: case 14: case 15: case 16: case 17:
    case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 30:
    case 31: case 35: case 36: case 44: case 45: case 46: case 47: case 60:
    case 61: case 64: case 65: case 66: case 67: case 68: case 69: case 84:
        return true;
    default:
        return (op >= 70 && op <= 78) || op >= 90;
    }
}

enum i650_status i650_step(struct I650 *m) {
    if (m->status != I650_RUNNING) {
        return m->status;
    }
    uint16_t n = m->address_register;
    if (!i650_valid_address(n)) {
        m->status = I650_STOP_STORAGE_SELECTION;
        return m->status;
    }
    uint64_t begin = m->word_time;
    uint64_t fetched = drum_wait_for(m->ready_at, n);
    m->program_register = drum_read(m, n);
    m->instruction_address = n;
    m->last_fetch_time = fetched;
    if (n < I650_DRUM_WORDS) {
        m->touched[n] |= I650_TOUCH_EXECUTED;
    }
    m->counters.instructions++;
    m->counters.idle_instruction += fetched > begin ? fetched - begin : 0;

    struct exec x;
    x.m = m;
    x.op = i650_op(m->program_register);
    x.d = i650_d_address(m->program_register);
    x.i = i650_i_address(m->program_register);
    x.t = fetched > m->interlock_until ? fetched : m->interlock_until;
    x.even = is_even(x.t);
    m->interlock_until = 0;

    struct i650_step_record *r = &m->last_step;
    r->address = n;
    r->instruction = m->program_register;
    r->begin = begin;
    r->fetched = fetched;
    r->start = x.t;
    r->has_data = false;

    /* Program-register control checks (GeneralInfo p.11): an unassigned
     * op code, or a D-address that isn't a machine address. */
    if (!known_op(x.op)) {
        error_stop(&x, I650_STOP_INVALID_OP, x.t);
    } else if (!i650_valid_address(x.d)) {
        error_stop(&x, I650_STOP_STORAGE_SELECTION, x.t);
    } else {
        dispatch(&x);
    }
    r->end = m->word_time;
    return m->status;
}

struct i650_word i650_peek(const struct I650 *m, uint16_t address) {
    return drum_read(m, address);
}

void i650_poke_drum(struct I650 *m, uint16_t address, struct i650_word value) {
    drum_write(m, address, value);
}

void i650_set_storage_entry(struct I650 *m, struct i650_word value) {
    m->storage_entry = value;
}

void i650_set_address_register(struct I650 *m, uint16_t address) {
    m->address_register = address;
}

void i650_reset_counters(struct I650 *m) {
    struct i650_counters zero = { m->word_time, 0, 0, 0 };
    m->counters = zero;
}

void i650_clear_drum(struct I650 *m) {
    for (unsigned a = 0; a < I650_DRUM_WORDS; a++) {
        m->drum[a] = i650_word_make(0, false);
        m->touched[a] = 0;
    }
}

bool i650_status_resumable(enum i650_status status) {
    return status == I650_HALTED || status == I650_STOP_OVERFLOW;
}

const char *i650_status_name(enum i650_status status) {
    switch (status) {
    case I650_RUNNING: return "running";
    case I650_HALTED: return "programmed stop";
    case I650_STOP_OVERFLOW: return "overflow stop";
    case I650_STOP_DIVIDE: return "divide check (quotient overflow)";
    case I650_STOP_STORAGE_SELECTION: return "storage selection error";
    case I650_STOP_INVALID_OP: return "invalid operation code";
    case I650_STOP_DISTRIBUTOR_DIGIT: return "branch-distributor digit neither 8 nor 9";
    case I650_STOP_NO_IO: return "card I/O not available";
    }
    return "unknown";
}

uint64_t i650_word_times_to_us(uint64_t word_times) {
    return word_times * I650_WORD_TIME_NS / 1000;
}
