#ifndef I650_CPU_H
#define I650_CPU_H

#include "types.h"

/* A new machine: drum and registers zero, word time 0, address register
 * 8000, Programmed switch on STOP, Overflow switch on SENSE. */
struct I650 *i650_create(void);
void i650_destroy(struct I650 *m);

/* Console COMPUTER RESET: clears the accumulator, distributor, program
 * register, overflow indicator and any stop, and sets the address
 * register to 8000 (GeneralInfo p.14), so the next instruction comes from
 * the storage-entry switches. Drum and switches are untouched, and the
 * clock keeps running -- the drum doesn't stop spinning. */
void i650_computer_reset(struct I650 *m);

/* Console PROGRAM START after a programmed (HLT) or overflow stop, or
 * with the card reader waiting for cards (the RD is tried again):
 * resumes at the address register. Any other stop needs a computer reset. */
void i650_program_start(struct I650 *m);

/* Reads, interprets and executes one instruction from the address
 * register, advancing the clock by however long the drum makes it take.
 * Returns the resulting status; when not RUNNING, the call is a no-op.
 * On an error stop the address register still points at the failing
 * instruction. */
enum i650_status i650_step(struct I650 *m);

/* --- Inspection / setup, for debuggers, loaders and tests ---
 * None of these cost drum time. */
struct i650_word i650_peek(const struct I650 *m, uint16_t address);
void i650_poke_drum(struct I650 *m, uint16_t address, struct i650_word value);
void i650_set_storage_entry(struct I650 *m, struct i650_word value);
void i650_set_address_register(struct I650 *m, uint16_t address);

/* Zero the debugger counters (the clock itself keeps running). */
void i650_reset_counters(struct I650 *m);

/* Clear the whole drum to plus zero and forget the access marks. */
void i650_clear_drum(struct I650 *m);

/* True for the stops Program Start can resume from (HLT, overflow). */
bool i650_status_resumable(enum i650_status status);

const char *i650_status_name(enum i650_status status);

/* Word times to microseconds (0.096 ms per word time). */
uint64_t i650_word_times_to_us(uint64_t word_times);

#endif
