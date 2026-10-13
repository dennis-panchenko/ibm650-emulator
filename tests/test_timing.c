/* Drum timing against the Optimum Program Chart (BasicOpcodes p.23) and
 * the worked optimizing examples on pp.24-29. Times are word times; at
 * time t the word at drum position t % 50 is under the heads. */
#include "test_util.h"
#include "../core/cpu.h"
#include "../core/drum.h"
#include "../core/types.h"

static struct I650 *m;

static void fresh(void) {
    if (m != NULL) {
        i650_destroy(m);
    }
    m = i650_create();
}

static void put(uint16_t at, unsigned op, uint16_t d, uint16_t i) {
    i650_poke_drum(m, at, i650_instruction(op, d, i));
}

/* Start at address n at word time n (so the first fetch has no wait). */
static void start_at(uint16_t n) {
    i650_set_address_register(m, n);
    m->ready_at = n;
}

TEST(wait_for_word) {
    ASSERT_EQ_INT(drum_wait_for(3, 5), 5, "two words ahead");
    ASSERT_EQ_INT(drum_wait_for(5, 5), 5, "already there");
    ASSERT_EQ_INT(drum_wait_for(6, 5), 55, "just missed: a full revolution");
    ASSERT_EQ_INT(drum_wait_for(3, 1955), 5, "any band: only the angle matters");
    ASSERT_EQ_INT(drum_wait_for(7, 8001), 7, "distributor is immediate");
    ASSERT_EQ_INT(drum_wait_for(7, 8002), 8, "lower accumulator: even word times");
    ASSERT_EQ_INT(drum_wait_for(8, 8003), 9, "upper accumulator: odd word times");
}

TEST(optimum_add_p28) {
    /* "if the data address of the add lower instruction had been 0004 the
     * search time would be zero ... if the instruction address had been
     * 0009, the search time for the next instruction would be zero" --
     * eight word times in all. */
    fresh();
    put(1, 15, 4, 9);
    put(9, 0, 0, 13);
    start_at(1);
    i650_step(m);
    ASSERT_EQ_INT(m->last_fetch_time, 1, "ALO read at 0001");
    i650_step(m);
    ASSERT_EQ_INT(m->last_fetch_time, 9, "next instruction read at 0009");
}

TEST(missed_instruction_costs_a_revolution) {
    fresh();
    put(1, 15, 4, 8);
    put(8, 0, 0, 13);
    start_at(1);
    i650_step(m);
    i650_step(m);
    ASSERT_EQ_INT(m->last_fetch_time, 58, "0008 had just gone by");
}

TEST(optimized_arithmetic_chain_fig41) {
    /* (A + B - C) x D, optimized: every access lands with zero wait. */
    fresh();
    put(48, 60, 1, 55);
    put(55, 10, 8, 13);
    put(13, 11, 16, 21);
    put(21, 19, 24, 25);
    start_at(48);
    i650_step(m);
    ASSERT_EQ_INT(m->last_fetch_time, 48, "RAU at 0048");
    i650_step(m);
    ASSERT_EQ_INT(m->last_fetch_time, 55, "AUP at 0055: d(0001)+4");
    i650_step(m);
    ASSERT_EQ_INT(m->last_fetch_time, 63, "SUP at 0013: d(0008)+5");
    i650_step(m);
    ASSERT_EQ_INT(m->last_fetch_time, 71, "MPY at 0021: d(0016)+5");
}

TEST(shift_limits_fig45) {
    /* SLT at 0025 (odd), 5 places: next instruction between 0031 and
     * 0037; SRT at 0037 likewise up to 0049, where MPY's data at 0002
     * is n+3 away. */
    fresh();
    put(25, 35, 5, 37);
    put(37, 30, 5, 49);
    put(49, 19, 2, 99);
    start_at(25);
    i650_step(m);
    i650_step(m);
    ASSERT_EQ_INT(m->last_fetch_time, 37, "SRT at the SLT's high limit");
    i650_step(m);
    ASSERT_EQ_INT(m->last_fetch_time, 49, "MPY at the SRT's high limit");
    ASSERT_EQ_INT(m->word_time, 52 + 21, "MPY data at 0002 with no wait, zero multiplier");
}

TEST(next_instruction_inside_shift_range_p25) {
    /* SRT at 0117, 7 places: the next instruction may sit anywhere in
     * 0123-0133 with no search time, but is optimized as if at 0133 --
     * so its optimum data address is 0136. */
    fresh();
    put(117, 30, 7, 125);
    put(125, 15, 136, 999);
    start_at(117);
    i650_step(m);
    i650_step(m);
    ASSERT_EQ_INT(m->last_fetch_time, 125, "read as soon as it comes round");
    ASSERT_EQ_INT(m->word_time, 136 + 5, "data at 0136 caught with no wait");
}

TEST(store_lower_waits_for_even_p31) {
    fresh();
    put(10, 20, 15, 18);
    put(18, 0, 0, 22);
    start_at(10);
    i650_step(m);
    i650_step(m);
    ASSERT_EQ_INT(m->last_fetch_time, 18, "STL at even n: data n+5, next d+3");
    fresh();
    put(11, 20, 15, 18);
    put(18, 0, 0, 22);
    start_at(11);
    i650_step(m);
    i650_step(m);
    ASSERT_EQ_INT(m->last_fetch_time, 18, "STL at odd n: data n+4");
}

TEST(accumulator_as_data_address_chart) {
    /* Chart, "Using 800X addresses": RAU 8002 -> next at n+9 (even n) or
     * n+8 (odd n); 8003 -> n+7 / n+8 (p.26-27). */
    struct { uint16_t n; uint16_t d; uint64_t next; } cases[] = {
        { 10, 8002, 19 }, { 11, 8002, 19 }, { 10, 8003, 17 }, { 11, 8003, 19 },
        { 10, 8000, 17 }, { 11, 8000, 19 },
    };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        fresh();
        put(cases[k].n, 60, cases[k].d, 999);
        start_at(cases[k].n);
        i650_step(m);
        ASSERT_EQ_INT(m->ready_at, cases[k].next, "next instruction time");
    }
}

TEST(branch_timing_chart) {
    fresh();
    m->acc.upper = 1;
    put(10, 44, 20, 30);
    start_at(10);
    i650_step(m);
    ASSERT_EQ_INT(m->ready_at, 13, "NZU even n, branch: n+3");
    fresh();
    put(10, 44, 20, 30);
    start_at(10);
    i650_step(m);
    ASSERT_EQ_INT(m->ready_at, 14, "NZU even n, no branch: n+4");
    fresh();
    put(11, 45, 20, 30);
    start_at(11);
    i650_step(m);
    ASSERT_EQ_INT(m->ready_at, 15, "NZE odd n, no branch: n+4");
}

TEST(multiply_time_depends_on_multiplier_digits) {
    fresh();
    m->acc.upper = 12;      /* digit sum 3 */
    put(10, 19, 14, 999);
    start_at(10);
    i650_step(m);
    ASSERT_EQ_INT(m->ready_at, 14 + 21 + 2 * 3, "d+21+2(sum of multiplier digits)");
}

TEST(divide_time_depends_on_quotient_digits) {
    fresh();
    m->acc.lower = 4682;
    i650_poke_drum(m, 13, i650_word_make(125, false));
    put(10, 14, 13, 999);
    start_at(10);
    i650_step(m);
    ASSERT_EQ_INT(m->ready_at, 13 + 60 + 2 * 10, "odd d: d+60+2(sum of quotient digits)");
}

TEST(complement_cycle_costs_two) {
    fresh();
    m->acc.lower = 5;
    i650_poke_drum(m, 13, i650_word_make(7, false));
    put(10, 16, 13, 999);
    start_at(10);
    i650_step(m);
    ASSERT_EQ_INT(m->ready_at, 13 + 4 + 2, "5 - 7 changes sign");
}

TEST(table_lookup_timing) {
    fresh();
    i650_poke_drum(m, 3, i650_word_make(50, false));
    m->distributor = i650_word_make(40, false);
    put(47, 84, 0, 999);
    start_at(47);
    i650_step(m);
    ASSERT_EQ_INT(m->ready_at, 53 + 6, "band starts at n+3, found at odd A: A+6");
}

TEST(optimum_beats_sequential) {
    /* Same five adds, laid out naively vs optimally: the reason optimum
     * programming existed. */
    fresh();
    for (uint16_t k = 0; k < 5; k++) {
        put((uint16_t)(1 + k), 15, (uint16_t)(100 + k), (uint16_t)(2 + k));
    }
    put(6, 1, 0, 6);
    start_at(1);
    while (i650_step(m) == I650_RUNNING) {
    }
    uint64_t sequential = m->word_time;

    fresh();
    uint16_t n = 1;
    for (uint16_t k = 0; k < 5; k++) {
        uint16_t d = (uint16_t)(n + 3);
        uint16_t next = (uint16_t)(d + (d % 2 == 0 ? 5 : 4));
        put(n, 15, (uint16_t)(d + 100), next);
        n = next;
    }
    put(n, 1, 0, n);
    start_at(1);
    while (i650_step(m) == I650_RUNNING) {
    }
    uint64_t optimum = m->word_time;
    ASSERT_TRUE(optimum * 5 < sequential, "optimum is several times faster");
    ASSERT_EQ_INT(i650_word_times_to_us(50), 4800, "one revolution is 4.8 ms");
}

TEST(step_record_and_idle_counters) {
    /* ALO at 0001, data 0004, next at 0008: ready at 9, so 0008 waits a
     * whole revolution less one word. */
    fresh();
    put(1, 15, 4, 8);
    put(8, 1, 0, 8);
    start_at(1);
    m->word_time = 1;
    i650_reset_counters(m);
    i650_step(m);
    const struct i650_step_record *r = &m->last_step;
    ASSERT_EQ_INT(r->address, 1, "address");
    ASSERT_EQ_INT(r->begin, 1, "begin");
    ASSERT_EQ_INT(r->fetched, 1, "no wait to read");
    ASSERT_TRUE(r->has_data && r->data_address == 4, "data address");
    ASSERT_EQ_INT(r->data_ready, 4, "data from n+3");
    ASSERT_EQ_INT(r->data_time, 4, "and there it is");
    ASSERT_EQ_INT(r->end, 9, "d+5");
    i650_step(m);
    ASSERT_EQ_INT(m->last_step.begin, 9, "next begins where the last ended");
    ASSERT_EQ_INT(m->last_step.fetched, 58, "0008 comes round again");
    ASSERT_EQ_INT(m->counters.instructions, 2, "instructions");
    ASSERT_EQ_INT(m->counters.idle_instruction, 49, "waited 49 word times");
    ASSERT_EQ_INT(m->counters.idle_data, 0, "no data wait");
    ASSERT_EQ_INT(m->word_time - m->counters.epoch, 61, "elapsed");
}

TEST(data_wait_is_counted) {
    fresh();
    put(10, 65, 12, 999);   /* data at n+3 = 13 wants 0012: a revolution */
    start_at(10);
    i650_reset_counters(m);
    i650_step(m);
    ASSERT_EQ_INT(m->last_step.data_time, 62, "0012 next comes round at 62");
    ASSERT_EQ_INT(m->counters.idle_data, 49, "data wait");
}

TEST(touch_marks) {
    fresh();
    put(10, 65, 13, 20);
    put(20, 20, 23, 30);
    start_at(10);
    i650_step(m);
    i650_step(m);
    ASSERT_EQ_INT(m->touched[10], I650_TOUCH_EXECUTED, "executed");
    ASSERT_EQ_INT(m->touched[13], I650_TOUCH_READ, "read");
    ASSERT_EQ_INT(m->touched[23], I650_TOUCH_WRITTEN, "written");
    i650_clear_drum(m);
    ASSERT_EQ_INT(m->touched[10], 0, "cleared");
}

int main(void) {
    RUN_TEST(wait_for_word);
    RUN_TEST(optimum_add_p28);
    RUN_TEST(missed_instruction_costs_a_revolution);
    RUN_TEST(optimized_arithmetic_chain_fig41);
    RUN_TEST(shift_limits_fig45);
    RUN_TEST(next_instruction_inside_shift_range_p25);
    RUN_TEST(store_lower_waits_for_even_p31);
    RUN_TEST(accumulator_as_data_address_chart);
    RUN_TEST(branch_timing_chart);
    RUN_TEST(multiply_time_depends_on_multiplier_digits);
    RUN_TEST(divide_time_depends_on_quotient_digits);
    RUN_TEST(complement_cycle_costs_two);
    RUN_TEST(table_lookup_timing);
    RUN_TEST(optimum_beats_sequential);
    RUN_TEST(step_record_and_idle_counters);
    RUN_TEST(data_wait_is_counted);
    RUN_TEST(touch_marks);
    i650_destroy(m);
    return test_util_report();
}
