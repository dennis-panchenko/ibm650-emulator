/* Cards, deck files and the 533: the fixed-board card layout, the read
 * feed (run-in, hopper empty, End of File), RD/WR through the machine,
 * load cards and the console bootstrap. OperMan = 22-6060-2. */
#include "test_util.h"
#include "../core/cards.h"
#include "../core/cpu.h"
#include "../core/types.h"

#include <string.h>

static struct I650 *m;

static void fresh(void) {
    if (m != NULL) {
        i650_destroy(m);
    }
    m = i650_create();
}

/* Puts cards (one string each) in the hopper and presses Read Start. */
static void hopper(const char *const *cards, size_t n) {
    struct i650_deck d = { 0 };
    for (size_t i = 0; i < n; i++) {
        struct i650_card c;
        i650_card_from_text(cards[i], &c);
        i650_deck_add(&d, &c);
    }
    i650_533_hopper_add(&m->io, &d);
    i650_533_read_start(&m->io);
    i650_deck_clear(&d);
}

/* Executes one instruction from 8000. */
static enum i650_status exec(unsigned op, uint16_t d, uint16_t i) {
    i650_set_storage_entry(m, i650_instruction(op, d, i));
    i650_set_address_register(m, I650_ADDR_SWITCHES);
    return i650_step(m);
}

/* An 80-column data card whose word 1 is n (plain digits, plus). */
static void data_card(char out[81], unsigned n) {
    memset(out, '0', 80);
    out[80] = '\0';
    for (int k = 9; k >= 0 && n > 0; k--, n /= 10) {
        out[k] = (char)('0' + n % 10);
    }
}

/* A load card (12 over column 1) for the bootstrap test's loader. */
static void loader_card(struct i650_card *c, uint16_t address, struct i650_word value) {
    struct i650_word w[I650_SYNC_WORDS] = {
        i650_instruction(69, 1954, 1953), i650_instruction(70, 1951, 8000),
        i650_instruction(24, address, 1952), value,
    };
    i650_card_from_words(w, c);
    c->col[0] = "{ABCDEFGHI"[c->col[0] - '0'];
}

TEST(sign_overpunch_characters) {
    struct i650_card c;
    struct i650_word w[I650_SYNC_WORDS];
    unsigned bad = 0;
    i650_card_from_text("000000012{000000012J000000012A00000001200000000000000000000000000000000000000000",
                        &c);
    ASSERT_TRUE(i650_card_to_words(&c, w, &bad), "valid card");
    ASSERT_EQ_INT(w[0].mag, 120, "12 over 0 ({) = +0 units");
    ASSERT_EQ_INT(w[0].neg, false, "{ is plus");
    ASSERT_EQ_INT(w[1].mag, 121, "11 over 1 (J)");
    ASSERT_EQ_INT(w[1].neg, true, "J is minus");
    ASSERT_EQ_INT(w[2].mag, 121, "12 over 1 (A)");
    ASSERT_EQ_INT(w[2].neg, false, "A is plus");
    ASSERT_EQ_INT(w[3].mag, 120, "plain digit");
    ASSERT_EQ_INT(w[3].neg, false, "no zone = plus");
    ASSERT_EQ_INT(w[8].mag, 0, "word 9 is zero");
    ASSERT_EQ_INT(w[9].mag, 0, "word 10 is zero");
}

TEST(blank_column_is_a_validity_error) {
    struct i650_card c;
    struct i650_word w[I650_SYNC_WORDS];
    unsigned bad = 0;
    i650_card_from_text("1234567890123", &c);   /* columns 14-80 blank */
    ASSERT_TRUE(!i650_card_to_words(&c, w, &bad), "blank columns");
    ASSERT_EQ_INT(bad, 14, "first blank column");
    i650_card_from_text("12345*7890", &c);
    ASSERT_TRUE(!i650_card_to_words(&c, w, &bad), "non-numeric column");
    ASSERT_EQ_INT(bad, 6, "the *");
}

TEST(punch_and_read_back) {
    struct i650_word in[I650_SYNC_WORDS], out[I650_SYNC_WORDS];
    for (unsigned k = 0; k < I650_SYNC_WORDS; k++) {
        in[k] = i650_word_make(1234567890ull * (k + 1) % I650_WORD_LIMIT, k % 3 == 1);
    }
    struct i650_card c;
    i650_card_from_words(in, &c);
    char text[81];
    i650_card_text(&c, text);
    ASSERT_EQ_INT(strlen(text), 80, "80 columns punched");
    ASSERT_EQ_INT(text[9], '{', "word 1 +, units 0: 12 over 0");
    ASSERT_EQ_INT(text[19], '}', "word 2 -, units 0: 11 over 0");
    unsigned bad;
    ASSERT_TRUE(i650_card_to_words(&c, out, &bad), "reads back");
    for (unsigned k = 0; k < I650_CARD_WORDS; k++) {
        ASSERT_EQ_INT(out[k].mag, in[k].mag, "magnitude");
        ASSERT_EQ_INT(out[k].neg, in[k].neg, "sign");
    }
    ASSERT_TRUE(!i650_card_is_load(&c), "punched cards aren't load cards");
}

TEST(deck_file_parsing) {
    struct i650_deck d = { 0 };
    unsigned line = 0;
    ASSERT_EQ_INT(i650_deck_parse("123\n\nABC\r\nlast", &d, &line), I650_DECK_OK, "parses");
    ASSERT_EQ_INT(d.count, 4, "empty line is a blank card; last line needs no newline");
    ASSERT_EQ_INT(d.cards[1].col[0], ' ', "blank card");
    ASSERT_EQ_INT(d.cards[2].col[3], ' ', "CR dropped, short line padded");
    i650_deck_clear(&d);
    char too_long[90];
    memset(too_long, '1', 81);
    too_long[81] = '\0';
    ASSERT_EQ_INT(i650_deck_parse(too_long, &d, &line), I650_DECK_TOO_LONG, "81 columns");
    i650_deck_clear(&d);
    ASSERT_EQ_INT(i650_deck_parse("12\n3\t4\n", &d, &line), I650_DECK_BAD_CHAR, "tab");
    ASSERT_EQ_INT(line, 2, "on line 2");
    i650_deck_clear(&d);
}

TEST(load_card_is_a_12_in_column_1) {
    struct i650_card c;
    i650_card_from_text("{000000020{", &c);
    ASSERT_TRUE(i650_card_is_load(&c), "12 over 0 in column 1");
    i650_card_from_text("0000000200", &c);
    ASSERT_TRUE(!i650_card_is_load(&c), "plain digit");
    i650_card_from_text("}000000200", &c);
    ASSERT_TRUE(!i650_card_is_load(&c), "11 punch");
}

TEST(run_in_feeds_three_cards) {
    /* OperMan p.21, condition 1: four or more cards, Read Start feeds 3. */
    fresh();
    const char *cards[] = { "1", "2", "3", "4", "5" };
    hopper(cards, 5);
    ASSERT_EQ_INT(m->io.in_feed, 3, "three cards in the feed");
    ASSERT_EQ_INT(i650_533_hopper_count(&m->io), 2, "two left in the hopper");
    ASSERT_TRUE(!i650_533_end_of_file(&m->io), "End of File needs an empty hopper");
}

TEST(rd_reads_into_the_band_and_feeds_the_next_card) {
    /* OperMan p.25: 70 0151 reads into 0151-0160; any D in the band will
     * do (0663 reads into 0651-0660). */
    fresh();
    char c1[81], c2[81], c3[81], c4[81];
    data_card(c1, 11);
    data_card(c2, 22);
    data_card(c3, 33);
    data_card(c4, 44);
    const char *cards[] = { c1, c2, c3, c4 };
    hopper(cards, 4);
    i650_poke_drum(m, 660, i650_word_make(777, false));
    ASSERT_EQ_INT(exec(70, 663, 999), I650_RUNNING, "RD");
    ASSERT_EQ_INT(i650_peek(m, 651).mag, 11, "word 1 to 0651");
    ASSERT_EQ_INT(i650_peek(m, 660).mag, 0, "word 10 to 0660 (zero)");
    ASSERT_EQ_INT(m->address_register, 999, "normal card: next from the I-address");
    ASSERT_EQ_INT(m->io.read_stacker.count, 1, "card 1 in the stacker");
    ASSERT_EQ_INT(m->io.in_feed, 3, "card 4 fed in behind");
    ASSERT_EQ_INT(i650_533_hopper_count(&m->io), 0, "hopper now empty");
}

TEST(rd_timing_read_cycle_and_interlock) {
    /* BasicOpcodes p.22: a 61 ms interlock per RD, 300 ms read cycle. */
    fresh();
    char c[81];
    data_card(c, 1);
    const char *cards[] = { c, c, c, c, c, c };
    hopper(cards, 6);
    exec(70, 1, 999);
    uint64_t first = m->last_step.start + 3;
    ASSERT_EQ_INT(m->last_step.end, first + I650_IO_INTERLOCK, "61 ms interlock");
    ASSERT_EQ_INT(m->io.read_ready, first + I650_READ_CYCLE, "300 ms cycle");
    exec(70, 1, 999);
    ASSERT_EQ_INT(m->last_step.end, first + I650_READ_CYCLE + I650_IO_INTERLOCK,
                  "second RD waits for the cycle");
    ASSERT_TRUE(m->last_step.data_time >= first + I650_READ_CYCLE, "data after the wait");
}

TEST(hopper_empty_waits_then_end_of_file) {
    /* OperMan p.23: with the hopper empty, the last three cards are still
     * in the feed; End of File lets the program process them. */
    fresh();
    char c1[81], c2[81], c3[81], c4[81];
    data_card(c1, 1);
    data_card(c2, 2);
    data_card(c3, 3);
    data_card(c4, 4);
    const char *cards[] = { c1, c2, c3, c4 };
    hopper(cards, 4);
    ASSERT_EQ_INT(exec(70, 1, 999), I650_RUNNING, "card 1");
    ASSERT_EQ_INT(exec(70, 1, 999), I650_STOP_READER, "hopper empty: reader waits");
    ASSERT_EQ_INT(m->address_register, 8000, "RD will be tried again");
    ASSERT_TRUE(i650_status_resumable(m->status), "Program Start can retry");
    ASSERT_TRUE(i650_533_end_of_file(&m->io), "End of File");
    i650_program_start(m);
    for (unsigned n = 2; n <= 4; n++) {
        ASSERT_EQ_INT(i650_step(m), I650_RUNNING, "processes a feed card");
        ASSERT_EQ_INT(i650_peek(m, 1).mag, n, "in order");
        i650_set_address_register(m, I650_ADDR_SWITCHES);
    }
    ASSERT_EQ_INT(i650_step(m), I650_STOP_READ_VALIDITY, "then blanks: read validity stop");
    ASSERT_EQ_INT(m->rvc_column, 1, "at column 1");
    ASSERT_TRUE(!i650_status_resumable(m->status), "needs computer reset");
    ASSERT_EQ_INT(m->io.read_stacker.count, 4, "all four in the stacker");
}

TEST(no_cards_reader_waits) {
    fresh();
    ASSERT_EQ_INT(exec(70, 1951, 999), I650_STOP_READER, "nothing in the reader");
    i650_computer_reset(m);
    ASSERT_EQ_INT(exec(70, 8001, 999), I650_STOP_STORAGE_SELECTION, "800X not an I/O area");
}

TEST(short_deck_needs_end_of_file) {
    /* Fewer than four cards: the hopper runs dry during run-in. */
    fresh();
    char c[81];
    data_card(c, 5);
    const char *cards[] = { c, c };
    hopper(cards, 2);
    ASSERT_EQ_INT(exec(70, 1, 999), I650_STOP_READER, "waits");
    i650_533_end_of_file(&m->io);
    i650_program_start(m);
    ASSERT_EQ_INT(i650_step(m), I650_RUNNING, "first card read");
    ASSERT_EQ_INT(i650_peek(m, 1).mag, 5, "data");
}

TEST(rvc_on_a_blank_column) {
    fresh();
    const char *cards[] = { "0000000001", "x", "x", "x" };
    hopper(cards, 4);
    ASSERT_EQ_INT(exec(70, 1, 999), I650_STOP_READ_VALIDITY, "columns 11-80 blank");
    ASSERT_EQ_INT(m->rvc_column, 11, "column 11");
}

TEST(load_card_branches_to_d_address) {
    /* OperMan p.46: a load card is read without the validity check and
     * the next instruction comes from the D-address. */
    fresh();
    const char *cards[] = { "{000000037{", "1", "1", "1" };
    hopper(cards, 4);
    ASSERT_EQ_INT(exec(70, 1801, 999), I650_RUNNING, "load card read");
    ASSERT_EQ_INT(m->address_register, 1801, "branch to the D-address");
    ASSERT_EQ_INT(i650_peek(m, 1801).mag, 37, "00 0000 0037 in 1801");
    ASSERT_EQ_INT(i650_peek(m, 1802).mag, 0, "blank columns read as zero");
    ASSERT_EQ_INT(i650_step(m), I650_RUNNING, "NOP");
    ASSERT_EQ_INT(m->address_register, 37, "on to 0037");
}

TEST(wr_punches_words_27_to_36) {
    /* OperMan p.26: 71 0123 punches from 0127-0136; drum unchanged. */
    fresh();
    i650_poke_drum(m, 127, i650_word_make(4321, true));
    i650_poke_drum(m, 134, i650_word_make(99, false));
    ASSERT_EQ_INT(exec(71, 123, 999), I650_RUNNING, "WR");
    ASSERT_EQ_INT(m->io.punch_stacker.count, 1, "one card punched");
    char text[81];
    i650_card_text(&m->io.punch_stacker.cards[0], text);
    ASSERT_TRUE(strncmp(text, "000000432J", 10) == 0, "word 1, 11 over the units");
    ASSERT_TRUE(strncmp(text + 70, "000000009I", 10) == 0, "word 8 from 0134");
    ASSERT_EQ_INT(i650_peek(m, 127).mag, 4321, "drum unchanged");
    uint64_t first = m->last_step.start + 3;
    exec(71, 123, 999);
    ASSERT_EQ_INT(m->last_step.end, first + I650_PUNCH_CYCLE + I650_IO_INTERLOCK,
                  "600 ms punch cycle");
}

TEST(other_io_units_stop) {
    fresh();
    ASSERT_EQ_INT(exec(72, 1, 999), I650_STOP_NO_IO, "RC needs a 537 or 407");
    ASSERT_EQ_INT(exec(73, 1, 999), I650_STOP_NO_IO, "synchronizer 2");
}

TEST(console_bootstrap_from_a_load_deck) {
    /* The console way to start: a one-word-per-card loader. Storage
     * entry 70 1951 8000 reads the first load card into 1951-1960 and
     * branches to 1951, where each card carries its own instructions:
     *     1951  LDD 1954 1953    the card's data word
     *     1953  STD tttt 1952    to its drum address
     *     1952  RD1 1951 8000    next card (a load card: back to 1951)
     * and the last card, 00 0000 0100 in word 1, jumps to the program. */
    fresh();
    struct i650_card deck[5];
    loader_card(&deck[0], 100, i650_instruction(60, 110, 104));    /* RAU 0110 0104 */
    loader_card(&deck[1], 104, i650_instruction(21, 120, 108));    /* STU 0120 0108 */
    loader_card(&deck[2], 108, i650_instruction(1, 0, 108));       /* HLT */
    loader_card(&deck[3], 110, i650_word_make(42, true));          /* the datum */
    i650_card_from_text("{00000010{", &deck[4]);                  /* transfer card */
    char text[5][81];
    const char *cards[5];
    for (int k = 0; k < 5; k++) {
        i650_card_text(&deck[k], text[k]);
        cards[k] = text[k];
    }
    hopper(cards, 5);
    i650_533_end_of_file(&m->io);   /* not yet: hopper still has cards */
    i650_set_storage_entry(m, i650_instruction(70, 1951, 8000));
    i650_computer_reset(m);
    int steps = 0;
    while (i650_step(m) == I650_RUNNING && steps < 100) {
        steps++;
    }
    ASSERT_EQ_INT(m->status, I650_STOP_READER, "hopper empty with 3 cards in the feed");
    ASSERT_TRUE(i650_533_end_of_file(&m->io), "operator presses End of File");
    i650_program_start(m);
    while (i650_step(m) == I650_RUNNING && steps < 200) {
        steps++;
    }
    ASSERT_EQ_INT(m->status, I650_HALTED, "loaded, transferred, ran");
    ASSERT_EQ_INT(m->instruction_address, 108, "HLT at 0108");
    ASSERT_EQ_INT(i650_peek(m, 120).mag, 42, "STU stored the datum");
    ASSERT_EQ_INT(i650_peek(m, 120).neg, true, "with its sign");
}

int main(void) {
    RUN_TEST(sign_overpunch_characters);
    RUN_TEST(blank_column_is_a_validity_error);
    RUN_TEST(punch_and_read_back);
    RUN_TEST(deck_file_parsing);
    RUN_TEST(load_card_is_a_12_in_column_1);
    RUN_TEST(run_in_feeds_three_cards);
    RUN_TEST(rd_reads_into_the_band_and_feeds_the_next_card);
    RUN_TEST(rd_timing_read_cycle_and_interlock);
    RUN_TEST(hopper_empty_waits_then_end_of_file);
    RUN_TEST(no_cards_reader_waits);
    RUN_TEST(short_deck_needs_end_of_file);
    RUN_TEST(rvc_on_a_blank_column);
    RUN_TEST(load_card_branches_to_d_address);
    RUN_TEST(wr_punches_words_27_to_36);
    RUN_TEST(other_io_units_stop);
    RUN_TEST(console_bootstrap_from_a_load_deck);
    i650_destroy(m);
    return test_util_report();
}
