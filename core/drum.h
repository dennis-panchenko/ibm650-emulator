/* Drum timing and storage access.
 *
 * Time is counted in word times: 12,500 rpm, 50 words per revolution,
 * 0.096 ms per word (GeneralInfo p.4). At word time t the word at angular
 * position t % 50 is under the heads -- i.e. drum addresses 0005, 0055,
 * 0105, ... (all 40 bands at once). Every instruction's timing is built
 * from this one primitive plus the offsets on the Optimum Program Chart
 * (BasicOpcodes p.23); see cpu.c. */
#ifndef I650_DRUM_H
#define I650_DRUM_H

#include "types.h"

#define I650_WORD_TIME_NS 96000ull

/* The first word time >= earliest at which address can be read.
 * Drum addresses wait for their angular position. The 800X registers
 * are electronic: 8000/8001 are immediate, but the lower accumulator
 * reads out only on even word times and the upper only on odd ones
 * (BasicOpcodes p.22). */
uint64_t drum_wait_for(uint64_t earliest, uint16_t address);

/* Contents of any valid address, no timing. 8002/8003 carry the
 * accumulator's sign. */
struct i650_word drum_read(const struct I650 *m, uint16_t address);

/* address must be 0000-1999 (stores to 800X are invalid, p.8). */
void drum_write(struct I650 *m, uint16_t address, struct i650_word value);

#endif
