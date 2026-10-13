/* Instruction semantics through the full machine, using the register
 * diagrams in BasicOpcodes (24-5002-0). Single instructions run from the
 * storage-entry switches (8000), the way an operator would try one out. */
#include "test_util.h"
#include "../core/cpu.h"
#include "../core/types.h"

static struct I650 *m;

static void fresh(void) {
    if (m != NULL) {
        i650_destroy(m);
    }
    m = i650_create();
}

static void set_acc(uint64_t upper, uint64_t lower, bool neg) {
    m->acc.upper = upper;
    m->acc.lower = lower;
    m->acc.neg = neg;
    m->acc.remainder_sign_valid = false;
}

static void poke(uint16_t a, uint64_t mag, bool neg) {
    i650_poke_drum(m, a, i650_word_make(mag, neg));
}

/* Executes one instruction from 8000, I-address 0999. */
static enum i650_status exec(unsigned op, uint16_t d) {
    i650_set_storage_entry(m, i650_instruction(op, d, 999));
    i650_set_address_register(m, I650_ADDR_SWITCHES);
    return i650_step(m);
}

#define ASSERT_ACC(u, l, n, msg)                                             \
    do {                                                                     \
        ASSERT_EQ_INT(m->acc.upper, (u), msg ": upper");                     \
        ASSERT_EQ_INT(m->acc.lower, (l), msg ": lower");                     \
        ASSERT_EQ_INT(m->acc.neg, (n), msg ": sign");                        \
    } while (0)

#define ASSERT_WORD(w, mg, n, msg)                                           \
    do {                                                                     \
        ASSERT_EQ_INT((w).mag, (mg), msg ": magnitude");                     \
        ASSERT_EQ_INT((w).neg, (n), msg ": sign");                           \
    } while (0)

TEST(alo_sequence_fig3) {
    fresh();
    poke(51, 4837910256ull, false);
    poke(52, 5411839270ull, false);
    poke(53, 1483925, true);
    exec(15, 51);
    ASSERT_ACC(0, 4837910256ull, false, "step 1");
    exec(15, 52);
    ASSERT_ACC(1, 249749526ull, false, "step 2");
    exec(15, 53);
    ASSERT_ACC(1, 248265601ull, false, "step 3");
    exec(15, 8001);
    ASSERT_ACC(1, 246781676ull, false, "step 4: distributor reused");
    exec(15, 8002);
    ASSERT_ACC(1, 493563352ull, false, "step 5: lower added to itself");
    exec(15, 8003);
    ASSERT_ACC(1, 493563353ull, false, "step 6: upper added to lower");
    ASSERT_TRUE(!m->overflow, "no overflow");
}

TEST(ral_fig4) {
    fresh();
    poke(51, 4837910256ull, false);
    poke(53, 1483925, true);
    set_acc(35121189, 4437682100ull, true);
    exec(65, 51);
    ASSERT_ACC(0, 4837910256ull, false, "A");
    exec(65, 53);
    ASSERT_ACC(0, 1483925, true, "B");
    exec(65, 8001);
    ASSERT_ACC(0, 1483925, true, "C");
    set_acc(35121189, 4437682100ull, false);
    exec(65, 8002);
    ASSERT_ACC(0, 4437682100ull, false, "D: keep lower, clear upper");
    set_acc(35121189, 4437682100ull, false);
    exec(65, 8003);
    ASSERT_ACC(0, 35121189, false, "E: upper shifted ten places right");
}

TEST(aup_overflow_fig5) {
    fresh();
    poke(2, 5837910256ull, false);
    poke(3, 1483925, true);
    set_acc(0, 4357, false);
    exec(10, 2);
    ASSERT_ACC(5837910256ull, 4357, false, "step 1");
    exec(10, 3);
    ASSERT_ACC(5836426331ull, 4357, false, "step 2");
    exec(10, 8001);
    ASSERT_ACC(5834942406ull, 4357, false, "step 3");
    exec(10, 8002);
    ASSERT_ACC(5834946763ull, 4357, false, "step 4");
    ASSERT_TRUE(!m->overflow, "no overflow yet");
    ASSERT_EQ_INT(exec(10, 8003), I650_RUNNING, "Overflow switch on SENSE: no stop");
    ASSERT_ACC(1669893526ull, 4357, false, "step 5");
    ASSERT_TRUE(m->overflow, "overflow indicator set");
}

TEST(rau_fig6) {
    fresh();
    poke(51, 4837910256ull, true);
    poke(52, 1483925, false);
    set_acc(35121189, 4437682100ull, false);
    exec(60, 51);
    ASSERT_ACC(4837910256ull, 0, true, "A");
    exec(60, 52);
    ASSERT_ACC(1483925, 0, false, "B");
    set_acc(35121189, 4437682100ull, false);
    exec(60, 8002);
    ASSERT_ACC(4437682100ull, 0, false, "D: lower shifted ten places left");
}

TEST(slo_fig7) {
    fresh();
    poke(250, 7148229653ull, true);
    poke(251, 5037148890ull, true);
    poke(252, 6182450031ull, false);
    set_acc(0, 21, true);
    exec(16, 250);
    ASSERT_ACC(0, 7148229632ull, false, "step 1");
    exec(16, 251);
    ASSERT_ACC(1, 2185378522ull, false, "step 2");
    exec(16, 252);
    ASSERT_ACC(0, 6002928491ull, false, "step 3");
}

TEST(rsl_fig8) {
    fresh();
    poke(97, 4911813, false);
    poke(1127, 10437256, true);
    set_acc(1782193, 4066510096ull, false);
    exec(66, 97);
    ASSERT_ACC(0, 4911813, true, "A");
    exec(66, 1127);
    ASSERT_ACC(0, 10437256, false, "B");
    set_acc(1782193, 4066510096ull, true);
    exec(66, 8003);
    ASSERT_ACC(0, 1782193, false, "D: minus upper, negated");
}

TEST(sup_fig9) {
    fresh();
    poke(915, 1483925, true);
    poke(100, 391466, false);
    poke(208, 1214653, false);
    set_acc(0, 48371, false);
    exec(11, 915);
    ASSERT_ACC(1483925, 48371, false, "step 1");
    exec(11, 100);
    ASSERT_ACC(1092459, 48371, false, "step 2");
    exec(11, 208);
    ASSERT_ACC(122193, 9999951629ull, true, "step 3: complement figure in lower");
}

TEST(rsu_fig10) {
    fresh();
    poke(31, 68413725, false);
    poke(320, 948153, true);
    set_acc(1041689357ull, 7801493325ull, false);
    exec(61, 31);
    ASSERT_ACC(68413725, 0, true, "A");
    exec(61, 320);
    ASSERT_ACC(948153, 0, false, "B");
}

TEST(absolute_codes_figs35_38) {
    fresh();
    poke(123, 539, true);
    poke(124, 539, false);
    set_acc(34328009ull, 1476590328ull, false);
    exec(17, 123);
    ASSERT_ACC(34328009ull, 1476590867ull, false, "17 AML: -539 added as +539");
    ASSERT_WORD(m->distributor, 539, true, "distributor keeps the real sign");
    set_acc(34328009ull, 1476590328ull, true);
    exec(17, 123);
    ASSERT_ACC(34328009ull, 1476589789ull, true, "17 AML into minus");
    exec(67, 123);
    ASSERT_ACC(0, 539, false, "67 RAM");
    set_acc(0, 123, false);
    exec(18, 123);
    ASSERT_ACC(0, 416, true, "18 SML");
    exec(68, 124);
    ASSERT_ACC(0, 539, true, "68 RSM");
    exec(68, 123);
    ASSERT_ACC(0, 539, true, "68 RSM of a minus factor");
}

TEST(multiply_and_divide_through_machine) {
    fresh();
    poke(231, 12345678, false);
    set_acc(8942711365ull, 0, false);
    exec(19, 231);
    ASSERT_ACC(11040383ull, 4959230470ull, false, "Fig.12A");
    ASSERT_WORD(m->distributor, 12345678, false, "multiplicand stays in distributor");

    poke(1783, 125, false);
    set_acc(0, 4682, false);
    exec(14, 1783);
    ASSERT_ACC(57, 37, false, "Fig.13 DIV");
    exec(64, 1783);
    ASSERT_ACC(0, 4560000000ull, false, "DVR: 570000000037 / 125, remainder dropped");
}

TEST(divide_overflow_always_stops) {
    fresh();
    poke(1016, 125, false);
    set_acc(852, 1034010200ull, false);
    ASSERT_EQ_INT(exec(14, 1016), I650_STOP_DIVIDE, "Fig.13 14-1016");
    ASSERT_EQ_INT(m->address_register, I650_ADDR_SWITCHES, "divide still in program register");
}

TEST(stores_figs16_19) {
    fresh();
    set_acc(404532877ull, 553882, true);
    exec(20, 1904);
    ASSERT_WORD(m->drum[1904], 553882, true, "Fig.16 STL");
    ASSERT_WORD(m->distributor, 553882, true, "via the distributor");
    exec(21, 5);
    ASSERT_WORD(m->drum[5], 404532877ull, true, "Fig.17 STU");

    set_acc(312, 4685381179ull, false);
    m->distributor = i650_word_make(3122768049ull, true);
    exec(22, 1955);
    ASSERT_WORD(m->drum[1955], 3185388049ull, true, "Fig.18 SDA, distributor's sign");
    m->distributor = i650_word_make(3122768049ull, true);
    exec(23, 1955);
    ASSERT_WORD(m->drum[1955], 3122761179ull, true, "Fig.19 SIA");
}

TEST(stu_after_divide_stores_remainder_sign_fig17) {
    fresh();
    poke(1783, 125, true);
    set_acc(0, 4682, false);
    exec(14, 1783);
    ASSERT_TRUE(m->acc.neg, "quotient minus");
    exec(21, 3);
    ASSERT_WORD(m->drum[3], 57, false, "remainder stored with its own plus sign");
    exec(65, 3);
    exec(21, 4);
    ASSERT_WORD(m->drum[4], 0, false, "reset clears the dual sign");
}

TEST(ldd_std_fig31) {
    fresh();
    poke(4, 6005120517ull, false);
    poke(27, 418310000ull, false);
    exec(69, 4);
    exec(24, 27);
    ASSERT_WORD(m->drum[27], 6005120517ull, false, "relocated");
}

TEST(store_to_800x_is_storage_selection_error) {
    fresh();
    ASSERT_EQ_INT(exec(24, 8001), I650_STOP_STORAGE_SELECTION, "STD 8001");
    fresh();
    ASSERT_EQ_INT(exec(20, 8000), I650_STOP_STORAGE_SELECTION, "STL 8000");
}

TEST(invalid_d_address_and_op) {
    fresh();
    ASSERT_EQ_INT(exec(65, 2000), I650_STOP_STORAGE_SELECTION, "D = 2000");
    fresh();
    ASSERT_EQ_INT(exec(0, 8004), I650_STOP_STORAGE_SELECTION, "NOP needs a valid D too");
    fresh();
    ASSERT_EQ_INT(exec(2, 0), I650_STOP_INVALID_OP, "op 02 unassigned");
    fresh();
    ASSERT_EQ_INT(exec(79, 0), I650_STOP_INVALID_OP, "op 79 unassigned");
    ASSERT_EQ_INT(i650_step(m), I650_STOP_INVALID_OP, "error stop is sticky");
    i650_program_start(m);
    ASSERT_EQ_INT(m->status, I650_STOP_INVALID_OP, "program start doesn't clear it");
    i650_computer_reset(m);
    ASSERT_EQ_INT(m->status, I650_RUNNING, "computer reset does");
}

TEST(branches_figs26_28) {
    fresh();
    set_acc(12345786, 0, false);
    exec(44, 155);
    ASSERT_EQ_INT(m->address_register, 155, "NZU branches on non-zero upper");
    set_acc(0, 8070643295ull, false);
    exec(44, 155);
    ASSERT_EQ_INT(m->address_register, 999, "NZU ignores the lower half");
    exec(45, 155);
    ASSERT_EQ_INT(m->address_register, 155, "NZE sees the lower half");
    set_acc(0, 0, true);
    exec(45, 155);
    ASSERT_EQ_INT(m->address_register, 999, "NZE: minus zero is zero");
    exec(46, 155);
    ASSERT_EQ_INT(m->address_register, 155, "BMI: minus zero is minus (Fig.28E)");
    set_acc(0, 123, false);
    exec(46, 155);
    ASSERT_EQ_INT(m->address_register, 999, "BMI on plus");
}

TEST(bov_tests_and_resets_overflow_fig29) {
    fresh();
    poke(103, 8431560792ull, false);
    poke(110, 3611842907ull, false);
    exec(60, 103);
    exec(10, 110);
    ASSERT_TRUE(m->overflow, "overflow set");
    exec(47, 118);
    ASSERT_EQ_INT(m->address_register, 118, "branch");
    ASSERT_TRUE(!m->overflow, "BOV resets the indicator");
    exec(47, 118);
    ASSERT_EQ_INT(m->address_register, 999, "no branch the second time");
}

TEST(overflow_switch_on_stop) {
    fresh();
    m->overflow_stop = true;
    poke(1, 9999999999ull, false);
    exec(60, 1);
    ASSERT_EQ_INT(exec(10, 1), I650_STOP_OVERFLOW, "stops");
    ASSERT_EQ_INT(m->address_register, 999, "after completing the instruction");
    i650_program_start(m);
    ASSERT_EQ_INT(m->status, I650_RUNNING, "restartable");
}

TEST(branch_distributor_fig30) {
    fresh();
    poke(10, 7918, false);
    exec(69, 10);
    exec(91, 516);
    ASSERT_EQ_INT(m->address_register, 516, "BD1: 8 in position 1");
    poke(10, 7989, false);
    exec(69, 10);
    exec(91, 516);
    ASSERT_EQ_INT(m->address_register, 999, "BD1: 9, no branch");
    exec(92, 521);
    ASSERT_EQ_INT(m->address_register, 521, "BD2: 8 in position 2");
    ASSERT_EQ_INT(exec(94, 526), I650_STOP_DISTRIBUTOR_DIGIT, "BD4: 7 is an error stop");
    fresh();
    m->distributor = i650_word_make(8000000000ull, false);
    exec(90, 600);
    ASSERT_EQ_INT(m->address_register, 600, "BD0 tests position 10");
}

TEST(halt_follows_programmed_switch) {
    fresh();
    ASSERT_EQ_INT(exec(1, 0), I650_HALTED, "switch on STOP");
    ASSERT_EQ_INT(m->address_register, 999, "restart goes to the I-address");
    i650_program_start(m);
    ASSERT_EQ_INT(m->status, I650_RUNNING, "program start resumes");
    m->programmed_stop = false;
    ASSERT_EQ_INT(exec(1, 0), I650_RUNNING, "switch on RUN: HLT is a NOP");
}

TEST(rd_without_cards_waits_for_the_reader) {
    fresh();
    ASSERT_EQ_INT(exec(70, 1951), I650_STOP_READER, "70 RD1, empty reader");
}

TEST(table_lookup_fig32) {
    fresh();
    poke(0, 1000650, false);
    poke(1, 2000658, false);
    poke(2, 3000016, false);
    m->distributor = i650_word_make(2000000, false);
    set_acc(100000478ull, 6048170115ull, false);
    exec(84, 0);
    ASSERT_ACC(100000478ull, 6000010115ull, false, "A: 0001 into positions 8-5");
    ASSERT_WORD(m->distributor, 2000000, false, "argument still in distributor");

    poke(0, 1000000, false);
    poke(1, 2000000, false);
    poke(2, 3000000, false);
    set_acc(100000478ull, 6048170115ull, false);
    exec(84, 3);
    ASSERT_ACC(100000478ull, 6000040115ull, false, "B: D-address 0003 + 1 passed");
}

TEST(table_lookup_skips_last_two_words_of_band) {
    fresh();
    for (uint16_t a = 0; a < 48; a++) {
        poke(a, a, false);
    }
    poke(48, 9999, false);  /* not table arguments */
    poke(49, 9999, false);
    poke(50, 100, false);
    m->distributor = i650_word_make(60, false);
    exec(84, 0);
    ASSERT_EQ_INT(i650_field(m->acc.lower, 8, 5), 50, "found in the next band");
}

TEST(table_lookup_off_the_end_stops) {
    fresh();
    m->distributor = i650_word_make(1, false);
    ASSERT_EQ_INT(exec(84, 1950), I650_STOP_STORAGE_SELECTION, "nothing >= 1 in 1950-1999");
}

TEST(tlu_program_fig33) {
    /* The gross-pay program: look up the employee's rate, multiply by
     * hours. The card read (8000: 70 0001 0140) is replaced by putting
     * the card's two words in 0001-0002 directly. */
    static const uint64_t table[10] = {
        217600185ull, 384100157ull, 1723800240ull, 1723900225ull, 1844800200ull,
        2001100225ull, 2058300215ull, 2145600225ull, 3411200210ull, 3548900160ull,
    };
    fresh();
    for (int k = 0; k < 10; k++) {
        poke((uint16_t)(500 + k), table[k], false);
    }
    poke(1, 2058300000ull, false);      /* employee 20583 */
    poke(2, 397, false);                /* 39.7 hours */
    poke(143, 6000000100ull, false);    /* skeleton 60 0000 0100 */
    i650_poke_drum(m, 140, i650_instruction(65, 143, 148));
    i650_poke_drum(m, 148, i650_instruction(69, 1, 104));
    i650_poke_drum(m, 104, i650_instruction(84, 500, 8002));
    i650_poke_drum(m, 100, i650_instruction(35, 5, 113));
    i650_poke_drum(m, 113, i650_instruction(30, 5, 125));
    i650_poke_drum(m, 125, i650_instruction(19, 2, 126));
    i650_poke_drum(m, 126, i650_instruction(1, 0, 126));
    i650_set_address_register(m, 140);
    int steps = 0;
    while (i650_step(m) == I650_RUNNING && steps < 20) {
        steps++;
        if (m->instruction_address == 104) {
            ASSERT_EQ_INT(m->acc.lower, 6005060100ull, "TLU built 60 0506 0100");
        }
    }
    ASSERT_EQ_INT(m->status, I650_HALTED, "ran to the HLT");
    ASSERT_ACC(0, 85355, false, "2.15 x 39.7 = 85.355");
}

int main(void) {
    RUN_TEST(alo_sequence_fig3);
    RUN_TEST(ral_fig4);
    RUN_TEST(aup_overflow_fig5);
    RUN_TEST(rau_fig6);
    RUN_TEST(slo_fig7);
    RUN_TEST(rsl_fig8);
    RUN_TEST(sup_fig9);
    RUN_TEST(rsu_fig10);
    RUN_TEST(absolute_codes_figs35_38);
    RUN_TEST(multiply_and_divide_through_machine);
    RUN_TEST(divide_overflow_always_stops);
    RUN_TEST(stores_figs16_19);
    RUN_TEST(stu_after_divide_stores_remainder_sign_fig17);
    RUN_TEST(ldd_std_fig31);
    RUN_TEST(store_to_800x_is_storage_selection_error);
    RUN_TEST(invalid_d_address_and_op);
    RUN_TEST(branches_figs26_28);
    RUN_TEST(bov_tests_and_resets_overflow_fig29);
    RUN_TEST(overflow_switch_on_stop);
    RUN_TEST(branch_distributor_fig30);
    RUN_TEST(halt_follows_programmed_switch);
    RUN_TEST(rd_without_cards_waits_for_the_reader);
    RUN_TEST(table_lookup_fig32);
    RUN_TEST(table_lookup_skips_last_two_words_of_band);
    RUN_TEST(table_lookup_off_the_end_stops);
    RUN_TEST(tlu_program_fig33);
    i650_destroy(m);
    return test_util_report();
}
