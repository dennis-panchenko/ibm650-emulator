/* ibm650: the interactive console, or --run for batch use. */
#include "cpu.h"
#include "display.h"
#include "program_loader.h"
#include "repl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_MAX_REV 100000ull

static void usage(FILE *f) {
    fputs("usage: ibm650 [program.650]              interactive console\n"
          "       ibm650 --run [--trace] [--max-rev N] program.650\n"
          "                                         run, report, exit 0 on HLT\n",
          f);
}

static int batch(const char *path, bool trace, unsigned long long max_rev) {
    struct I650 *m = i650_create();
    if (m == NULL || !program_loader_load(m, path)) {
        i650_destroy(m);
        return 2;
    }
    uint64_t limit = m->word_time + max_rev * I650_WORDS_PER_BAND;
    if (trace) {
        display_trace_header(stdout, true);
    }
    while (m->status == I650_RUNNING && m->word_time < limit) {
        i650_step(m);
        if (trace) {
            display_trace_line(stdout, m, &m->last_step, true);
        }
    }
    char counters[160];
    display_counters(m, counters, sizeof counters);
    int rc;
    if (m->status == I650_RUNNING) {
        printf("gave up after %llu revolutions, next instruction at %04u \xc2\xb7 %s\n",
               max_rev, (unsigned)m->address_register, counters);
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
    i650_destroy(m);
    return rc;
}

int main(int argc, char **argv) {
    bool run = false, trace = false;
    unsigned long long max_rev = DEFAULT_MAX_REV;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--run") == 0) {
            run = true;
        } else if (strcmp(argv[i], "--trace") == 0) {
            trace = true;
        } else if (strcmp(argv[i], "--max-rev") == 0 && i + 1 < argc) {
            char *end;
            max_rev = strtoull(argv[++i], &end, 10);
            if (*end != '\0' || max_rev == 0) {
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
        if (path == NULL) {
            usage(stderr);
            return 2;
        }
        return batch(path, trace, max_rev);
    }
    struct I650 *m = i650_create();
    if (m == NULL) {
        return 1;
    }
    int rc = repl_run(m, path);
    i650_destroy(m);
    return rc;
}
