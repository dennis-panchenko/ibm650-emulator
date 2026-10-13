#include "program_loader.h"
#include "cards.h"
#include "program.h"
#include "selfload.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return NULL;
    }
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    size_t got;
    while (buf != NULL && (got = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += got;
        if (cap - len - 1 == 0) {
            char *bigger = realloc(buf, cap * 2);
            if (bigger == NULL) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = bigger;
            cap *= 2;
        }
    }
    fclose(f);
    if (buf == NULL) {
        fprintf(stderr, "%s: out of memory\n", path);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

bool program_loader_load(struct I650 *m, const char *path) {
    char *text = read_file(path);
    if (text == NULL) {
        return false;
    }
    struct i650_program *p = malloc(sizeof *p);
    struct i650_program_error err;
    bool ok = p != NULL && i650_program_parse(text, p, &err);
    if (p == NULL) {
        fprintf(stderr, "%s: out of memory\n", path);
    } else if (!ok) {
        fprintf(stderr, "%s:%u:%u: %s\n", path, err.line, err.column, err.message);
    } else {
        i650_program_load(m, p);
    }
    free(p);
    free(text);
    return ok;
}

bool program_loader_deck(const char *path, const char *deck_path, size_t *cards) {
    char *text = read_file(path);
    if (text == NULL) {
        return false;
    }
    struct i650_program *p = malloc(sizeof *p);
    struct i650_program_error err;
    struct i650_deck deck = { 0 };
    bool ok = p != NULL && i650_program_parse(text, p, &err);
    if (p == NULL) {
        fprintf(stderr, "%s: out of memory\n", path);
    } else if (!ok) {
        fprintf(stderr, "%s:%u:%u: %s\n", path, err.line, err.column, err.message);
    } else {
        uint16_t at = 0;
        switch (i650_program_deck(p, &deck, &at)) {
        case I650_SELFLOAD_OK:
            ok = deck_loader_save(deck_path, &deck);
            *cards = deck.count;
            break;
        case I650_SELFLOAD_NO_START:
            fprintf(stderr, "%s: a self-loading deck needs a start line "
                    "(its transfer card goes there)\n", path);
            ok = false;
            break;
        case I650_SELFLOAD_READ_AREA:
            fprintf(stderr, "%s:%u: %04u is in 1951-1960, the loader's read area\n",
                    path, p->line_of[at], (unsigned)at);
            ok = false;
            break;
        case I650_SELFLOAD_NO_MEMORY:
            fprintf(stderr, "%s: out of memory\n", path);
            ok = false;
            break;
        }
    }
    i650_deck_clear(&deck);
    free(p);
    free(text);
    return ok;
}

bool deck_loader_read(const char *path, struct i650_deck *deck) {
    char *text = read_file(path);
    if (text == NULL) {
        return false;
    }
    unsigned line = 0;
    enum i650_deck_parse r = i650_deck_parse(text, deck, &line);
    free(text);
    switch (r) {
    case I650_DECK_OK:
        return true;
    case I650_DECK_TOO_LONG:
        fprintf(stderr, "%s:%u: more than 80 columns\n", path, line);
        break;
    case I650_DECK_BAD_CHAR:
        fprintf(stderr, "%s:%u: not a card character (printable ASCII only)\n", path, line);
        break;
    case I650_DECK_NO_MEMORY:
        fprintf(stderr, "%s: out of memory\n", path);
        break;
    }
    return false;
}

bool deck_loader_save(const char *path, const struct i650_deck *deck) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return false;
    }
    char text[I650_CARD_COLUMNS + 1];
    for (size_t i = 0; i < deck->count; i++) {
        i650_card_text(&deck->cards[i], text);
        fprintf(f, "%s\n", text);
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return false;
    }
    return true;
}
