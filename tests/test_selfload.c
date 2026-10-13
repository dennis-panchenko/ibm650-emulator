/* .650 programs as self-loading decks: Andree's one-instruction-per-card
 * load routine (Programming the IBM 650, 1958, section 2.6, pp.30-31). */
#include "test_util.h"
#include "../core/cards.h"
#include "../core/cpu.h"
#include "../core/program.h"
#include "../core/selfload.h"
#include "../core/types.h"

#include <stdlib.h>
#include <string.h>

static struct i650_program prog;
static struct i650_deck deck;

static enum i650_selfload build(const char *source, uint16_t *at) {
    struct i650_program_error err;
    i650_deck_clear(&deck);
    if (!i650_program_parse(source, &prog, &err)) {
        fprintf(stderr, "  parse error line %u: %s\n", err.line, err.message);
        return I650_SELFLOAD_NO_MEMORY;
    }
    return i650_program_deck(&prog, &deck, at);
}

static const char *card(size_t k) {
    static char text[I650_CARD_COLUMNS + 1];
    i650_card_text(&deck.cards[k], text);
    return text;
}

/* Boots the deck from the console as the operator would, pressing End of
 * File when the reader waits. Returns the machine (caller destroys). */
static struct I650 *boot(void) {
    struct I650 *m = i650_create();
    i650_533_hopper_add(&m->io, &deck);
    i650_set_storage_entry(m, i650_word_make(I650_SELFLOAD_ENTRY, false));
    i650_computer_reset(m);
    i650_533_read_start(&m->io);
    for (int steps = 0; steps < 10000 && m->status == I650_RUNNING; steps++) {
        if (i650_step(m) == I650_STOP_READER && i650_533_end_of_file(&m->io)) {
            i650_program_start(m);
        }
    }
    return m;
}

TEST(card_layout_is_andrees) {
    /* Word 1 69 1954 1953 with the load punch over column 1, word 3
     * 24 AAAA 8000, word 4 the word; words 2 and 5-8 blank (p.30). */
    uint16_t at;
    ASSERT_EQ_INT(build("start 0100\n0100 RAU 0110 0104\n0110 42-\n", &at), I650_SELFLOAD_OK,
                  "builds");
    ASSERT_EQ_INT(deck.count, 3, "two words and the transfer card");
    ASSERT_TRUE(strcmp(card(0), "F91954195C          240100800{600110010D") == 0, "0100");
    ASSERT_TRUE(strcmp(card(1), "F91954195C          240110800{000000004K") == 0, "0110, minus");
    ASSERT_TRUE(strcmp(card(2), "{00000010{") == 0, "transfer card 00 0000 0100");
    ASSERT_TRUE(i650_card_is_load(&deck.cards[0]), "a load card");
    ASSERT_TRUE(i650_card_is_load(&deck.cards[2]), "transfer card is a load card");
}

TEST(needs_a_start_and_a_free_read_area) {
    uint16_t at = 0;
    ASSERT_EQ_INT(build("0100 HLT 0000 0100\n", &at), I650_SELFLOAD_NO_START, "no start");
    ASSERT_EQ_INT(build("start 0100\n0100 HLT 0000 0100\n1957 5\n", &at),
                  I650_SELFLOAD_READ_AREA, "1957 is in the read area");
    ASSERT_EQ_INT(at, 1957, "names the address");
    ASSERT_EQ_INT(build("start 0100\n0100 HLT 0000 0100\n1950 5\n1961 5\n", &at),
                  I650_SELFLOAD_OK, "1950 and 1961 are fine");
}

TEST(booted_deck_matches_the_program) {
    uint16_t at;
    ASSERT_EQ_INT(build("start 0048\n"
                        "0048 RAU 0001 0055\n0055 AUP 0008 0013\n0013 SUP 0016 0021\n"
                        "0021 MPY 0024 0051\n0051 STL 0105 0058\n0058 HLT 0000 0058\n"
                        "0001 12+\n0008 25+\n0016 7+\n0024 4+\n1999 0-\n0000 9999999999-\n",
                        &at),
                  I650_SELFLOAD_OK, "builds");
    struct I650 *m = boot();
    ASSERT_EQ_INT(m->status, I650_HALTED, "loaded, transferred, ran to the HLT");
    ASSERT_EQ_INT(m->instruction_address, 58, "HLT at 0058");
    ASSERT_EQ_INT(i650_peek(m, 105).mag, 120, "(12 + 25 - 7) x 4");
    for (uint16_t a = 0; a < I650_DRUM_WORDS; a++) {
        if (prog.present[a]) {
            struct i650_word w = i650_peek(m, a);
            ASSERT_TRUE(w.mag == prog.words[a].mag && w.neg == prog.words[a].neg,
                        "every program word on the drum, sign included");
        }
    }
    ASSERT_EQ_INT(m->io.read_stacker.count, deck.count, "every card read");
    i650_destroy(m);
}

TEST(a_non_load_card_stops_at_9000) {
    /* Andree p.31: a card without the high punch reads through the board
     * and the RD goes to 9000 -- a storage selection stop. */
    uint16_t at;
    build("start 0100\n0100 HLT 0000 0100\n", &at);
    deck.count = 1;     /* drop the transfer card ... */
    struct i650_card data;
    char text[I650_CARD_COLUMNS + 1];
    memset(text, '0', I650_CARD_COLUMNS);
    text[9] = '1';
    text[I650_CARD_COLUMNS] = '\0';
    i650_card_from_text(text, &data);
    i650_deck_add(&deck, &data);    /* ... and follow with a data card */
    struct I650 *m = boot();
    ASSERT_EQ_INT(m->status, I650_STOP_STORAGE_SELECTION, "storage selection stop");
    ASSERT_EQ_INT(m->address_register, 9000, "at 9000");
    ASSERT_EQ_INT(i650_peek(m, 1951).mag, 1, "the data card is in 1951");
    i650_destroy(m);
}

int main(void) {
    RUN_TEST(card_layout_is_andrees);
    RUN_TEST(needs_a_start_and_a_free_read_area);
    RUN_TEST(booted_deck_matches_the_program);
    RUN_TEST(a_non_load_card_stops_at_9000);
    i650_deck_clear(&deck);
    return test_util_report();
}
