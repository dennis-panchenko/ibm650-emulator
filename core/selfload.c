#include "selfload.h"
#include "cards.h"

#include <string.h>

/* Blanks card word w (0-based). */
static void blank_word(struct i650_card *card, unsigned w) {
    memset(card->col + w * 10, ' ', 10);
}

static bool add_load_card(struct i650_deck *out, const struct i650_word words[I650_SYNC_WORDS],
                          const bool punched[I650_CARD_WORDS]) {
    struct i650_card card;
    i650_card_from_words(words, &card);
    for (unsigned w = 0; w < I650_CARD_WORDS; w++) {
        if (!punched[w]) {
            blank_word(&card, w);
        }
    }
    i650_card_mark_load(&card);
    return i650_deck_add(out, &card);
}

enum i650_selfload i650_program_deck(const struct i650_program *p, struct i650_deck *out,
                                     uint16_t *at) {
    if (!p->has_start) {
        return I650_SELFLOAD_NO_START;
    }
    for (uint16_t a = I650_SELFLOAD_FIRST; a <= I650_SELFLOAD_LAST; a++) {
        if (p->present[a]) {
            *at = a;
            return I650_SELFLOAD_READ_AREA;
        }
    }
    struct i650_word words[I650_SYNC_WORDS];
    const bool loader[I650_CARD_WORDS] = { true, false, true, true };
    for (uint16_t a = 0; a < I650_DRUM_WORDS; a++) {
        if (!p->present[a]) {
            continue;
        }
        memset(words, 0, sizeof words);
        words[0] = i650_instruction(69, 1954, 1953);
        words[2] = i650_instruction(24, a, 8000);
        words[3] = p->words[a];
        if (!add_load_card(out, words, loader)) {
            return I650_SELFLOAD_NO_MEMORY;
        }
    }
    const bool transfer[I650_CARD_WORDS] = { true };
    memset(words, 0, sizeof words);
    words[0] = i650_instruction(0, 0, p->start);
    if (!add_load_card(out, words, transfer)) {
        return I650_SELFLOAD_NO_MEMORY;
    }
    return I650_SELFLOAD_OK;
}
