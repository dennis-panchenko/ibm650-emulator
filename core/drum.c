#include "drum.h"

uint64_t drum_wait_for(uint64_t earliest, uint16_t address) {
    switch (address) {
    case I650_ADDR_SWITCHES:
    case I650_ADDR_DISTRIBUTOR:
        return earliest;
    case I650_ADDR_LOWER:
        return earliest + (earliest % 2);
    case I650_ADDR_UPPER:
        return earliest + 1 - (earliest % 2);
    default: {
        uint64_t here = earliest % I650_WORDS_PER_BAND;
        uint64_t there = address % I650_WORDS_PER_BAND;
        return earliest + (there + I650_WORDS_PER_BAND - here) % I650_WORDS_PER_BAND;
    }
    }
}

struct i650_word drum_read(const struct I650 *m, uint16_t address) {
    switch (address) {
    case I650_ADDR_SWITCHES:
        return m->storage_entry;
    case I650_ADDR_DISTRIBUTOR:
        return m->distributor;
    case I650_ADDR_LOWER:
        return i650_word_make(m->acc.lower, m->acc.neg);
    case I650_ADDR_UPPER:
        return i650_word_make(m->acc.upper, m->acc.neg);
    default:
        return m->drum[address];
    }
}

void drum_write(struct I650 *m, uint16_t address, struct i650_word value) {
    m->drum[address] = value;
}
