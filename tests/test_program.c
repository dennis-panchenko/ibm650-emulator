/* The .650 program file parser and loader. */
#include "test_util.h"
#include "../core/cpu.h"
#include "../core/opcodes.h"
#include "../core/program.h"

#include <string.h>

static struct i650_program prog;
static struct i650_program_error err;

static bool parse(const char *text) {
    return i650_program_parse(text, &prog, &err);
}

TEST(mnemonics_round_trip) {
    ASSERT_EQ_INT(i650_opcode("RAU"), 60, "RAU");
    ASSERT_EQ_INT(i650_opcode("tlu"), 84, "case-insensitive");
    ASSERT_EQ_INT(i650_opcode("BD0"), 90, "BD0 is 90 (position 10)");
    ASSERT_EQ_INT(i650_opcode("RAUX"), -1, "no prefix matches");
    ASSERT_EQ_INT(i650_opcode("XYZ"), -1, "unknown");
    ASSERT_TRUE(i650_mnemonic(2) == NULL, "02 unassigned");
    for (unsigned op = 0; op < 100; op++) {
        const char *n = i650_mnemonic(op);
        if (n != NULL) {
            ASSERT_EQ_INT(i650_opcode(n), (int)op, n);
        }
    }
}

TEST(instructions_words_and_start) {
    ASSERT_TRUE(parse("; a comment\n"
                      "start 0048\n"
                      "0048  RAU 0001 0055   ; trailing comment\n"
                      "0055  10 8 13\n"
                      "\n"
                      "0001  25+\n"
                      "0002\t6000010055-\n"
                      "0003  7"),
                "parses");
    ASSERT_TRUE(prog.has_start && prog.start == 48, "start");
    ASSERT_EQ_INT(prog.words[48].mag, 6000010055ull, "mnemonic instruction");
    ASSERT_EQ_INT(prog.words[55].mag, 1000080013ull, "numeric op, short addresses");
    ASSERT_EQ_INT(prog.words[1].mag, 25, "word");
    ASSERT_TRUE(prog.words[2].neg && prog.words[2].mag == 6000010055ull, "signed 10 digits");
    ASSERT_TRUE(prog.present[3] && !prog.words[3].neg, "no sign is plus");
    ASSERT_TRUE(!prog.present[4], "absent");
}

TEST(errors_carry_line_and_column) {
    ASSERT_TRUE(!parse("0001 25+\n0001 26+\n"), "duplicate");
    ASSERT_EQ_INT(err.line, 2, "line");
    ASSERT_TRUE(strcmp(err.message, "address already used on line 1") == 0, err.message);

    ASSERT_TRUE(!parse("0010  FOO 0001 0002\n"), "bad op");
    ASSERT_EQ_INT(err.column, 7, "column of the op");

    ASSERT_TRUE(!parse("2000 1+\n"), "off the drum");
    ASSERT_TRUE(!parse("0001 12345678901+\n"), "11 digits");
    ASSERT_TRUE(!parse("0001 RAU 0001\n"), "missing I");
    ASSERT_TRUE(!parse("0001 RAU 0001 0002 0003 0004\n"), "too many");
    ASSERT_TRUE(!parse("start 2500\n"), "invalid start");
}

TEST(load_clears_and_resets) {
    struct I650 *m = i650_create();
    i650_poke_drum(m, 7, i650_word_make(99, false));
    m->acc.lower = 5;
    m->counters.instructions = 3;
    ASSERT_TRUE(parse("start 0100\n0100 HLT 0000 0100\n"), "parses");
    i650_program_load(m, &prog);
    ASSERT_EQ_INT(m->drum[7].mag, 0, "drum cleared");
    ASSERT_EQ_INT(m->acc.lower, 0, "computer reset");
    ASSERT_EQ_INT(m->counters.instructions, 0, "counters zeroed");
    ASSERT_EQ_INT(m->address_register, 100, "start");
    ASSERT_EQ_INT(i650_step(m), I650_HALTED, "runs");

    ASSERT_TRUE(parse("0100 HLT 0000 0100\n"), "parses");
    i650_program_load(m, &prog);
    ASSERT_EQ_INT(m->address_register, 8000, "no start: storage-entry switches");
    i650_destroy(m);
}

int main(void) {
    RUN_TEST(mnemonics_round_trip);
    RUN_TEST(instructions_words_and_start);
    RUN_TEST(errors_carry_line_and_column);
    RUN_TEST(load_clears_and_resets);
    return test_util_report();
}
