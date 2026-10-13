/* ibm650: the interactive console, or --run for batch use. */
#include "cards.h"
#include "cpu.h"
#include "display.h"
#include "program_loader.h"
#include "repl.h"

#include "program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_MAX_REV 100000ull

#define MAX_DECKS 16

struct options {
    bool trace;
    unsigned long long max_rev;
    const char *decks[MAX_DECKS];
    unsigned deck_count;
    const char *punch;
    bool has_entry;
    struct i650_word entry;
};

static void usage(FILE *f) {
    fputs("usage: ibm650 [program.650]              interactive console\n"
          "       ibm650 --run [options] [program.650]\n"
          "                                         run, report, exit 0 on HLT\n"
          "options:\n"
          "  --trace          a trace line per instruction\n"
          "  --max-rev N      give up after N drum revolutions (default 100000)\n"
          "  --hopper DECK    put a deck in the read hopper (repeatable); End of\n"
          "                   File is pressed whenever the reader runs dry\n"
          "  --punch FILE     save the punched cards\n"
          "  --entry WORD     storage-entry switches, e.g. 7019518000 (no program:\n"
          "                   start from them, the console bootstrap)\n",
          f);
}

static int batch(const char *path, const struct options *o) {
    struct I650 *m = i650_create();
    if (m == NULL) {
        return 2;
    }
    if (o->has_entry) {
        i650_set_storage_entry(m, o->entry);
    }
    if (path != NULL && !program_loader_load(m, path)) {
        i650_destroy(m);
        return 2;
    }
    for (unsigned k = 0; k < o->deck_count; k++) {
        struct i650_deck deck = { 0 };
        bool ok = deck_loader_read(o->decks[k], &deck) && i650_533_hopper_add(&m->io, &deck);
        i650_deck_clear(&deck);
        if (!ok) {
            i650_destroy(m);
            return 2;
        }
    }
    i650_533_read_start(&m->io);
    bool trace = o->trace;
    unsigned long long max_rev = o->max_rev;
    uint64_t limit = m->word_time + max_rev * I650_WORDS_PER_BAND;
    if (trace) {
        display_trace_header(stdout, true);
    }
    while (m->status == I650_RUNNING && m->word_time < limit) {
        i650_step(m);
        if (trace) {
            display_trace_line(stdout, m, &m->last_step, true);
        }
        /* The operator's job when the hopper runs dry. */
        if (m->status == I650_STOP_READER && i650_533_end_of_file(&m->io)) {
            if (trace) {
                printf("(end of file pressed)\n");
            }
            i650_program_start(m);
        }
    }
    char counters[160];
    display_counters(m, counters, sizeof counters);
    int rc;
    if (m->status == I650_RUNNING) {
        printf("gave up after %llu revolutions, next instruction at %04u \xc2\xb7 %s\n",
               max_rev, (unsigned)m->address_register, counters);
        rc = 1;
    } else if (m->status == I650_STOP_READ_VALIDITY) {
        printf("%s at %04u, card column %u \xc2\xb7 %s\n", i650_status_name(m->status),
               (unsigned)m->address_register, m->rvc_column, counters);
        rc = 1;
    } else if (m->status == I650_HALTED) {
        printf("HLT at %04u \xc2\xb7 %s\n", (unsigned)m->instruction_address, counters);
        rc = 0;
    } else {
        printf("%s at %04u \xc2\xb7 %s\n", i650_status_name(m->status),
               (unsigned)m->address_register, counters);
        rc = 1;
    }
    display_panel(stdout, m, false);
    if (o->punch != NULL && !deck_loader_save(o->punch, &m->io.punch_stacker)) {
        rc = 2;
    }
    i650_destroy(m);
    return rc;
}

int main(int argc, char **argv) {
    bool run = false;
    struct options o = { 0 };
    o.max_rev = DEFAULT_MAX_REV;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--run") == 0) {
            run = true;
        } else if (strcmp(argv[i], "--trace") == 0) {
            o.trace = true;
        } else if (strcmp(argv[i], "--hopper") == 0 && i + 1 < argc && o.deck_count < MAX_DECKS) {
            o.decks[o.deck_count++] = argv[++i];
        } else if (strcmp(argv[i], "--punch") == 0 && i + 1 < argc) {
            o.punch = argv[++i];
        } else if (strcmp(argv[i], "--entry") == 0 && i + 1 < argc) {
            if (!i650_program_parse_word(argv[++i], &o.entry)) {
                fprintf(stderr, "bad --entry word '%s'\n", argv[i]);
                return 2;
            }
            o.has_entry = true;
        } else if (strcmp(argv[i], "--max-rev") == 0 && i + 1 < argc) {
            char *end;
            o.max_rev = strtoull(argv[++i], &end, 10);
            if (*end != '\0' || o.max_rev == 0) {
                usage(stderr);
                return 2;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (argv[i][0] == '-' || path != NULL) {
            usage(stderr);
            return 2;
        } else {
            path = argv[i];
        }
    }
    if (run) {
        if (path == NULL && o.deck_count == 0) {
            usage(stderr);
            return 2;
        }
        return batch(path, &o);
    }
    struct I650 *m = i650_create();
    if (m == NULL) {
        return 1;
    }
    int rc = repl_run(m, path);
    i650_destroy(m);
    return rc;
}
