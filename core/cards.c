#include "cards.h"

#include <stdlib.h>
#include <string.h>

static const char plus_zone[] = "{ABCDEFGHI";
static const char minus_zone[] = "}JKLMNOPQR";

bool i650_deck_add(struct i650_deck *d, const struct i650_card *card) {
    if (d->count == d->cap) {
        size_t cap = d->cap == 0 ? 64 : d->cap * 2;
        struct i650_card *bigger = realloc(d->cards, cap * sizeof *bigger);
        if (bigger == NULL) {
            return false;
        }
        d->cards = bigger;
        d->cap = cap;
    }
    d->cards[d->count++] = *card;
    return true;
}

void i650_deck_clear(struct i650_deck *d) {
    free(d->cards);
    d->cards = NULL;
    d->count = 0;
    d->cap = 0;
}

enum i650_deck_parse i650_deck_parse(const char *text, struct i650_deck *out,
                                     unsigned *bad_line) {
    unsigned line = 0;
    const char *p = text;
    while (*p != '\0') {
        line++;
        const char *end = strchr(p, '\n');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        if (len > 0 && p[len - 1] == '\r') {
            len--;
        }
        *bad_line = line;
        if (len > I650_CARD_COLUMNS) {
            return I650_DECK_TOO_LONG;
        }
        struct i650_card card;
        memset(card.col, ' ', sizeof card.col);
        for (size_t i = 0; i < len; i++) {
            if (p[i] < ' ' || p[i] > '~') {
                return I650_DECK_BAD_CHAR;
            }
            card.col[i] = p[i];
        }
        if (!i650_deck_add(out, &card)) {
            return I650_DECK_NO_MEMORY;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
    return I650_DECK_OK;
}

void i650_card_text(const struct i650_card *card, char out[I650_CARD_COLUMNS + 1]) {
    size_t n = I650_CARD_COLUMNS;
    while (n > 0 && card->col[n - 1] == ' ') {
        n--;
    }
    memcpy(out, card->col, n);
    out[n] = '\0';
}

void i650_card_from_text(const char *text, struct i650_card *out) {
    memset(out->col, ' ', sizeof out->col);
    for (size_t i = 0; i < I650_CARD_COLUMNS && text[i] != '\0'; i++) {
        out->col[i] = text[i];
    }
}

/* A column's digit and zone: zone is 0 (none), 12 or 11. False if the
 * column holds no digit (blank, or a character with no digit punch). */
static bool column_digit(char c, unsigned *digit, unsigned *zone) {
    const char *z;
    if (c >= '0' && c <= '9') {
        *digit = (unsigned)(c - '0');
        *zone = 0;
        return true;
    }
    if (c != '\0' && (z = strchr(plus_zone, c)) != NULL) {
        *digit = (unsigned)(z - plus_zone);
        *zone = 12;
        return true;
    }
    if (c != '\0' && (z = strchr(minus_zone, c)) != NULL) {
        *digit = (unsigned)(z - minus_zone);
        *zone = 11;
        return true;
    }
    return false;
}

static bool card_words(const struct i650_card *card, struct i650_word out[I650_SYNC_WORDS],
                       bool check, unsigned *bad) {
    for (unsigned w = 0; w < I650_SYNC_WORDS; w++) {
        out[w] = i650_word_make(0, false);
    }
    for (unsigned w = 0; w < I650_CARD_WORDS; w++) {
        uint64_t mag = 0;
        bool neg = false;
        for (unsigned k = 0; k < 10; k++) {
            unsigned column = w * 10 + k;
            unsigned digit = 0, zone = 0;
            if (!column_digit(card->col[column], &digit, &zone)) {
                if (check) {
                    *bad = column + 1;
                    return false;
                }
                digit = 0;
            }
            mag = mag * 10 + digit;
            if (k == 9) {
                neg = zone == 11;
            }
        }
        out[w] = i650_word_make(mag, neg);
    }
    return true;
}

bool i650_card_to_words(const struct i650_card *card, struct i650_word out[I650_SYNC_WORDS],
                        unsigned *bad) {
    return card_words(card, out, true, bad);
}

void i650_card_to_words_load(const struct i650_card *card,
                             struct i650_word out[I650_SYNC_WORDS]) {
    unsigned unused;
    card_words(card, out, false, &unused);
}

bool i650_card_is_load(const struct i650_card *card) {
    unsigned digit, zone;
    return column_digit(card->col[I650_LOAD_COLUMN - 1], &digit, &zone) && zone == 12;
}

void i650_card_mark_load(struct i650_card *card) {
    char *c = &card->col[I650_LOAD_COLUMN - 1];
    unsigned digit, zone;
    if (!column_digit(*c, &digit, &zone)) {
        digit = 0;
    }
    *c = plus_zone[digit];
}

void i650_card_from_words(const struct i650_word words[I650_SYNC_WORDS],
                          struct i650_card *out) {
    for (unsigned w = 0; w < I650_CARD_WORDS; w++) {
        uint64_t mag = words[w].mag;
        for (int k = 9; k >= 0; k--) {
            unsigned digit = (unsigned)(mag % 10);
            mag /= 10;
            char c = (char)('0' + digit);
            if (k == 9) {
                c = words[w].neg ? minus_zone[digit] : plus_zone[digit];
            }
            out->col[w * 10 + (unsigned)k] = c;
        }
    }
}

/* --- The 533, operator side --- */

size_t i650_533_hopper_count(const struct i650_533 *u) {
    return u->hopper.count - u->hopper_next;
}

bool i650_533_hopper_add(struct i650_533 *u, const struct i650_deck *deck) {
    /* Drop the cards already fed before growing the hopper. */
    if (u->hopper_next > 0) {
        size_t left = i650_533_hopper_count(u);
        memmove(u->hopper.cards, u->hopper.cards + u->hopper_next, left * sizeof *u->hopper.cards);
        u->hopper.count = left;
        u->hopper_next = 0;
    }
    for (size_t i = 0; i < deck->count; i++) {
        if (!i650_deck_add(&u->hopper, &deck->cards[i])) {
            return false;
        }
    }
    return true;
}

unsigned i650_533_read_start(struct i650_533 *u) {
    unsigned fed = 0;
    while (u->in_feed < I650_FEED_CARDS && i650_533_hopper_count(u) > 0) {
        u->feed[u->in_feed++] = u->hopper.cards[u->hopper_next++];
        fed++;
    }
    return fed;
}

bool i650_533_end_of_file(struct i650_533 *u) {
    i650_533_read_start(u);
    if (i650_533_hopper_count(u) > 0 || u->in_feed == 0) {
        return false;
    }
    u->eof = true;
    return true;
}

void i650_533_runout(struct i650_533 *u) {
    for (unsigned i = 0; i < u->in_feed; i++) {
        i650_deck_add(&u->read_stacker, &u->feed[i]);
    }
    u->in_feed = 0;
    u->hopper.count = 0;
    u->hopper_next = 0;
    u->eof = false;
}

void i650_533_free(struct i650_533 *u) {
    i650_deck_clear(&u->hopper);
    i650_deck_clear(&u->read_stacker);
    i650_deck_clear(&u->punch_stacker);
    u->hopper_next = 0;
    u->in_feed = 0;
    u->eof = false;
}
