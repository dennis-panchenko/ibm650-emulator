/* The console: a plain line-oriented REPL. Frequent commands are one
 * letter; a run is stopped by pressing Enter (or Ctrl-C), checked between
 * instructions, so the terminal stays in its normal line mode. */
#define _POSIX_C_SOURCE 200809L

#include "repl.h"
#include "cards.h"
#include "cpu.h"
#include "display.h"
#include "drum.h"
#include "program.h"
#include "program_loader.h"
#include "selfload.h"

#include <ctype.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define LINE_MAX_LEN 256
#define MEMORY_LINES 10
#define FAST_POLL_STEPS 4096
#define NATIVE_POLL_STEPS 1024

enum speed { SPEED_STEP, SPEED_FAST, SPEED_NATIVE, SPEED_WALK };

struct session {
    struct I650 *m;
    char path[1024];
    bool trace;                 /* trace lines during r / f */
    bool bar;                   /* timing bar in trace lines */
    double walk_seconds;        /* one drum revolution in walk mode */
    bool breakpoints[I650_DRUM_WORDS];
    bool interactive;           /* stdin is a terminal */
    bool tty_out;               /* stdout is a terminal */
    uint16_t memory_next;       /* where a bare `m` continues */
    char repeat[LINE_MAX_LEN];  /* what an empty line repeats */
    char pending[LINE_MAX_LEN]; /* typed during a run; runs after the stop */
    bool eof;
    bool in_trace_block;        /* last output was a trace line: no new header */
    bool auto_eof;              /* booting: press End of File when the reader waits */
};

static volatile sig_atomic_t interrupted;

static void on_sigint(int sig) {
    (void)sig;
    interrupted = 1;
}

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void strip_newline(char *line) {
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
        line[--n] = '\0';
    }
}

/* Waits up to ms for the user to stop the run. A line typed meanwhile
 * (just Enter, or a command) stops it; a command is kept in pending. */
static bool wait_for_stop(struct session *s, int ms) {
    if (interrupted) {
        interrupted = 0;
        return true;
    }
    if (!s->interactive) {
        if (ms > 0) {
            struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
            nanosleep(&ts, NULL);
        }
    } else {
        struct pollfd p = { STDIN_FILENO, POLLIN, 0 };
        if (poll(&p, 1, ms) > 0) {
            if (fgets(s->pending, sizeof s->pending, stdin) == NULL) {
                s->pending[0] = '\0';
                s->eof = true;
            }
            strip_newline(s->pending);
            return true;
        }
    }
    if (interrupted) {
        interrupted = 0;
        return true;
    }
    return false;
}

/* Walk mode: the bar grows as the slowed drum turns, one word time per
 * cell (a revolution takes walk_seconds). */
static bool walk_line(struct session *s) {
    struct I650 *m = s->m;
    const struct i650_step_record *r = &m->last_step;
    int cell_ms = (int)(s->walk_seconds * 1000.0 / I650_WORDS_PER_BAND + 0.5);
    if (cell_ms < 1) {
        cell_ms = 1;
    }
    char bar[DISPLAY_BAR_MAX + 1];
    size_t len = display_bar(r, bar, DISPLAY_BAR_MAX);
    if (!s->tty_out || !s->bar) {
        display_trace_line(stdout, m, r, s->bar);
        fflush(stdout);
        size_t cells = len < DISPLAY_BAR_MAX ? len : DISPLAY_BAR_MAX;
        return wait_for_stop(s, (int)cells * cell_ms);
    }
    char prefix[64];
    display_trace_prefix(m, r, prefix, sizeof prefix);
    fputs(prefix, stdout);
    bool stop = false;
    size_t shown = len > DISPLAY_BAR_MAX ? DISPLAY_BAR_MAX : len;
    for (size_t k = 0; k < len; k++) {
        if (k < shown) {
            bool cut = len > DISPLAY_BAR_MAX && k >= DISPLAY_BAR_MAX - 3;
            putchar(cut ? '.' : bar[k]);
            fflush(stdout);
        }
        /* Beyond the bar's width (long card-I/O interlocks) the drum
         * isn't slowed: a 300 ms read cycle would take two minutes. */
        if (!stop && k < DISPLAY_BAR_MAX) {
            stop = wait_for_stop(s, cell_ms);
        }
    }
    printf("%*s %3llu\n", (int)(DISPLAY_BAR_MAX - shown), "", (unsigned long long)len);
    return stop;
}

static void report_stop(struct session *s, const char *head) {
    s->in_trace_block = false;
    char c[160];
    display_counters(s->m, c, sizeof c);
    printf("%s \xc2\xb7 %s\n", head, c);
}

static void report_machine_stop(struct session *s) {
    struct I650 *m = s->m;
    char head[128];
    switch (m->status) {
    case I650_HALTED:
        snprintf(head, sizeof head, "HLT at %04u", (unsigned)m->instruction_address);
        break;
    case I650_STOP_OVERFLOW:
        snprintf(head, sizeof head, "overflow stop after %04u",
                 (unsigned)m->instruction_address);
        break;
    case I650_STOP_READER:
        snprintf(head, sizeof head, "card reader waiting at %04u (%s)",
                 (unsigned)m->address_register,
                 m->io.in_feed > 0 ? "hopper empty -- hopper <deck> or eof, then r"
                                   : "no cards -- hopper <deck>, then r");
        break;
    case I650_STOP_READ_VALIDITY:
        snprintf(head, sizeof head, "read validity check at %04u: card column %u",
                 (unsigned)m->address_register, m->rvc_column);
        break;
    default:
        snprintf(head, sizeof head, "%s at %04u", i650_status_name(m->status),
                 (unsigned)m->address_register);
        break;
    }
    report_stop(s, head);
}

/* r, f, w and s: run until the machine stops, an address stop, the user
 * stops it, or (limit > 0) that many instructions. */
static void run(struct session *s, enum speed speed, unsigned long limit) {
    struct I650 *m = s->m;
    if (m->status != I650_RUNNING) {
        if (!i650_status_resumable(m->status)) {
            printf("machine stopped (%s at %04u) -- c for computer reset\n",
                   i650_status_name(m->status), (unsigned)m->address_register);
            return;
        }
        i650_program_start(m);
    }
    bool show = speed == SPEED_STEP || s->trace;
    uint64_t wt0 = m->word_time;
    uint64_t wall0 = now_us();
    unsigned long steps = 0;
    interrupted = 0;
    for (;;) {
        if (m->status == I650_STOP_READER && s->auto_eof && i650_533_end_of_file(&m->io)) {
            puts("(end of file pressed)");
            s->in_trace_block = false;
            i650_program_start(m);
        }
        if (m->status != I650_RUNNING) {
            report_machine_stop(s);
            return;
        }
        if (steps > 0 && m->address_register < I650_DRUM_WORDS &&
            s->breakpoints[m->address_register]) {
            char head[32];
            snprintf(head, sizeof head, "address stop at %04u", (unsigned)m->address_register);
            report_stop(s, head);
            return;
        }
        if (limit > 0 && steps >= limit) {
            return;
        }
        i650_step(m);
        steps++;
        if ((show || speed == SPEED_WALK) && !s->in_trace_block) {
            display_trace_header(stdout, s->bar);
            s->in_trace_block = true;
        }
        bool stop = false;
        if (speed == SPEED_WALK) {
            stop = walk_line(s);
        } else {
            if (show) {
                display_trace_line(stdout, m, &m->last_step, s->bar);
            }
            if (speed == SPEED_NATIVE) {
                int64_t ahead = (int64_t)i650_word_times_to_us(m->word_time - wt0) -
                                (int64_t)(now_us() - wall0);
                if (ahead > 2000) {
                    fflush(stdout);
                    stop = wait_for_stop(s, (int)(ahead / 1000));
                } else if (steps % NATIVE_POLL_STEPS == 0) {
                    stop = wait_for_stop(s, 0);
                }
            } else if (speed == SPEED_FAST && steps % FAST_POLL_STEPS == 0) {
                stop = wait_for_stop(s, 0);
            } else if (interrupted) {
                interrupted = 0;
                stop = true;
            }
        }
        if (stop) {
            if (m->status != I650_RUNNING) {
                report_machine_stop(s);
            } else {
                char head[48];
                snprintf(head, sizeof head, "stopped, next instruction at %04u",
                         (unsigned)m->address_register);
                report_stop(s, head);
            }
            return;
        }
    }
}

/* --- Argument parsing --- */

static bool parse_address(const char *arg, uint16_t *out) {
    if (arg == NULL || strlen(arg) == 0 || strlen(arg) > 4) {
        return false;
    }
    unsigned v = 0;
    for (const char *p = arg; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
        v = v * 10 + (unsigned)(*p - '0');
    }
    if (!i650_valid_address((uint16_t)v)) {
        return false;
    }
    *out = (uint16_t)v;
    return true;
}

static bool parse_count(const char *arg, unsigned long *out) {
    if (arg == NULL) {
        return false;
    }
    char *end;
    unsigned long v = strtoul(arg, &end, 10);
    if (*arg == '\0' || *end != '\0' || v == 0) {
        return false;
    }
    *out = v;
    return true;
}

/* --- Commands --- */

static void help(void) {
    puts("Running (any of these stop with Enter or Ctrl-C):\n"
         "  s [n]          step one instruction (or n); empty line repeats\n"
         "  r              run at the 650's own speed (Program Start)\n"
         "  f              run as fast as possible\n"
         "  w [seconds]    walk: drum slowed to one revolution per 2 s (or seconds)\n"
         "Looking:\n"
         "  p [b]          panel: registers, switches, counters (b: bi-quinary lamps)\n"
         "  m addr [n]     show n words from addr (m alone continues)\n"
         "  m addr = word  change a word, e.g. m 0107 = 25+\n"
         "  v [addr]       drum map: bands x drum positions (addr: just its band)\n"
         "  t              trace each instruction during r and f, on/off\n"
         "  d              timing bar in trace lines on/off\n"
         "Console:\n"
         "  b [addr]       toggle an address stop (b alone lists them)\n"
         "  g addr         transfer: set the address register\n"
         "  c              computer reset (address register to 8000)\n"
         "  k              show switches;  k entry <word>,  k prog stop|run,\n"
         "                 k ovfl stop|sense\n"
         "  z              zero the counters\n"
         "Cards (the 533 reader and punch):\n"
         "  hopper [deck...]  put decks in the read hopper and press Read Start;\n"
         "                 hopper alone shows the 533; hopper clear runs the feed out\n"
         "  eof            End of File: let the program read the last cards\n"
         "  punch [n]      the last n punched cards;  punch save <file>,  punch clear\n"
         "  stacker [n]    the last n cards read;  stacker clear\n"
         "  boot deck [data...]  load and run a self-loading deck from the console\n"
         "                 (entry 70 1951 9000, reset, start; End of File pressed)\n"
         "Program:\n"
         "  l [file]       load a .650 file (l alone reloads)\n"
         "  e [file]       edit in $VISUAL / $EDITOR, then reload\n"
         "  deck file      write the loaded .650 as a self-loading deck\n"
         "  ? / q          help / quit\n"
         "\n"
         "Trace bar, one character per word time (0.096 ms, 1/50 revolution):\n"
         "  . waiting for the drum   I instruction read   D data   = busy\n"
         "\n"
         "For command history and line editing, run under rlwrap: rlwrap ibm650");
}

static void load(struct session *s, const char *arg) {
    if (arg != NULL) {
        snprintf(s->path, sizeof s->path, "%s", arg);
    }
    if (s->path[0] == '\0') {
        puts("usage: l <file.650>");
        return;
    }
    if (program_loader_load(s->m, s->path)) {
        printf("loaded %s; address register %04u\n", s->path,
               (unsigned)s->m->address_register);
        s->memory_next = s->m->address_register < I650_DRUM_WORDS ? s->m->address_register : 0;
    } else {
        puts("drum unchanged");
    }
}

static void edit(struct session *s, const char *arg) {
    if (arg != NULL) {
        snprintf(s->path, sizeof s->path, "%s", arg);
    }
    if (s->path[0] == '\0') {
        puts("usage: e <file.650>");
        return;
    }
    const char *editor = getenv("VISUAL");
    if (editor == NULL || *editor == '\0') {
        editor = getenv("EDITOR");
    }
    if (editor == NULL || *editor == '\0') {
        editor = "vi";
    }
    char command[512];
    snprintf(command, sizeof command, "%s \"$1\"", editor);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", command, "sh", s->path, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0) {
        puts("could not start the editor");
        return;
    }
    interrupted = 0;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        printf("editor exited with an error; not reloading\n");
        return;
    }
    if (access(s->path, R_OK) != 0) {
        printf("%s not saved; nothing to load\n", s->path);
        return;
    }
    load(s, NULL);
}

static void memory(struct session *s, char *a1, char *a2, char *a3) {
    struct I650 *m = s->m;
    uint16_t at = s->memory_next;
    if (a1 != NULL && !parse_address(a1, &at)) {
        puts("usage: m <address> [count]  or  m <address> = <word>");
        return;
    }
    if (a2 != NULL && strcmp(a2, "=") == 0) {
        struct i650_word w;
        if (a3 == NULL || !i650_program_parse_word(a3, &w)) {
            puts("usage: m <address> = <word>, e.g. m 0107 = 25+");
            return;
        }
        if (at < I650_DRUM_WORDS) {
            i650_poke_drum(m, at, w);
        } else if (at == I650_ADDR_SWITCHES) {
            i650_set_storage_entry(m, w);
        } else {
            puts("only drum words and 8000 (the storage-entry switches) can be set");
            return;
        }
        display_memory_line(stdout, m, at, at < I650_DRUM_WORDS && s->breakpoints[at]);
        return;
    }
    unsigned long count = MEMORY_LINES;
    if (a2 != NULL && !parse_count(a2, &count)) {
        puts("usage: m <address> [count]");
        return;
    }
    if (at >= I650_DRUM_WORDS) {
        display_memory_line(stdout, m, at, false);
        return;
    }
    for (unsigned long k = 0; k < count && at < I650_DRUM_WORDS; k++, at++) {
        display_memory_line(stdout, m, at, s->breakpoints[at]);
    }
    s->memory_next = at < I650_DRUM_WORDS ? at : 0;
}

static void breakpoint(struct session *s, const char *arg) {
    if (arg == NULL) {
        bool any = false;
        for (unsigned a = 0; a < I650_DRUM_WORDS; a++) {
            if (s->breakpoints[a]) {
                printf("%s%04u", any ? " " : "address stops: ", a);
                any = true;
            }
        }
        puts(any ? "" : "no address stops");
        return;
    }
    uint16_t a;
    if (!parse_address(arg, &a) || a >= I650_DRUM_WORDS) {
        puts("usage: b <drum address>");
        return;
    }
    s->breakpoints[a] = !s->breakpoints[a];
    printf("address stop %s at %04u\n", s->breakpoints[a] ? "set" : "cleared", (unsigned)a);
}

static void switches(struct session *s, const char *name, const char *value) {
    struct I650 *m = s->m;
    if (name != NULL) {
        if (value == NULL) {
            puts("usage: k entry <word> | k prog stop|run | k ovfl stop|sense");
            return;
        }
        if (strcmp(name, "entry") == 0) {
            struct i650_word w;
            if (!i650_program_parse_word(value, &w)) {
                puts("bad word: 1-10 digits, optional sign, e.g. 7000010100+");
                return;
            }
            i650_set_storage_entry(m, w);
        } else if (strcmp(name, "prog") == 0 && strcmp(value, "stop") == 0) {
            m->programmed_stop = true;
        } else if (strcmp(name, "prog") == 0 && strcmp(value, "run") == 0) {
            m->programmed_stop = false;
        } else if (strcmp(name, "ovfl") == 0 && strcmp(value, "stop") == 0) {
            m->overflow_stop = true;
        } else if (strcmp(name, "ovfl") == 0 && strcmp(value, "sense") == 0) {
            m->overflow_stop = false;
        } else {
            puts("usage: k entry <word> | k prog stop|run | k ovfl stop|sense");
            return;
        }
    }
    char entry[12];
    i650_format_word(m->storage_entry, entry);
    printf("storage entry %s \xc2\xb7 programmed %s \xc2\xb7 overflow %s\n", entry,
           m->programmed_stop ? "STOP" : "RUN", m->overflow_stop ? "STOP" : "SENSE");
}

static void show_533(struct session *s) {
    char io[128];
    display_533(s->m, io, sizeof io);
    puts(io);
}

/* line is the whole command line: "hopper a.deck b.deck". */
/* Puts each deck named by arg and the strtok words after it in the
 * hopper, then presses Read Start. False if a deck couldn't be read. */
static bool add_decks(struct session *s, char *arg) {
    struct i650_533 *u = &s->m->io;
    for (; arg != NULL; arg = strtok(NULL, " \t")) {
        struct i650_deck deck = { 0 };
        if (!deck_loader_read(arg, &deck)) {
            i650_deck_clear(&deck);
            puts("hopper unchanged");
            return false;
        }
        if (!i650_533_hopper_add(u, &deck)) {
            puts("out of memory");
        } else {
            printf("%s: %zu card%s\n", arg, deck.count, deck.count == 1 ? "" : "s");
        }
        i650_deck_clear(&deck);
        u->eof = false;
    }
    i650_533_read_start(u);
    return true;
}

static void hopper(struct session *s, char *line) {
    struct i650_533 *u = &s->m->io;
    strtok(line, " \t");
    char *arg = strtok(NULL, " \t");
    if (arg != NULL && strcmp(arg, "clear") == 0) {
        size_t had = u->in_feed;
        i650_533_runout(u);
        printf("hopper emptied, %zu card%s run out to the stacker\n", had, had == 1 ? "" : "s");
        show_533(s);
        return;
    }
    add_decks(s, arg);
    show_533(s);
}

/* Boots self-loading decks the way the operator did (Andree p.34):
 * deck in the hopper, storage-entry switches 70 1951 9000, Computer
 * Reset, Program Start, Read Start -- and End of File when the reader
 * waits for the last cards. Cards still in the feed are run out first. */
static void boot(struct session *s, char *line) {
    strtok(line, " \t");
    char *arg = strtok(NULL, " \t");
    if (arg == NULL) {
        puts("usage: boot <deck> [data deck...]");
        return;
    }
    i650_533_runout(&s->m->io);
    if (!add_decks(s, arg)) {
        return;
    }
    i650_set_storage_entry(s->m, i650_word_make(I650_SELFLOAD_ENTRY, false));
    i650_computer_reset(s->m);
    i650_reset_counters(s->m);
    puts("storage entry 70 1951 9000, computer reset, program start");
    s->auto_eof = true;
    run(s, SPEED_NATIVE, 0);
    s->auto_eof = false;
}

/* Writes the loaded .650 file as a self-loading deck. */
static void deck(struct session *s, const char *arg) {
    size_t cards = 0;
    if (arg == NULL || s->path[0] == '\0') {
        puts(s->path[0] == '\0' ? "no .650 file loaded" : "usage: deck <file.cards>");
        return;
    }
    if (program_loader_deck(s->path, arg, &cards)) {
        printf("%s: %zu cards from %s; boot %s\n", arg, cards, s->path, arg);
    }
}

static void end_of_file(struct session *s) {
    if (i650_533_end_of_file(&s->m->io)) {
        puts("end of file: the program can read the last cards (r to continue)");
    } else if (s->m->io.in_feed == 0) {
        puts("no cards in the feed");
    } else {
        puts("end of file only works with the hopper empty");
    }
    show_533(s);
}

static void punch(struct session *s, const char *a1, const char *a2) {
    struct i650_deck *d = &s->m->io.punch_stacker;
    if (a1 != NULL && strcmp(a1, "save") == 0) {
        if (a2 == NULL) {
            puts("usage: punch save <file>");
        } else if (deck_loader_save(a2, d)) {
            printf("%zu card%s saved to %s\n", d->count, d->count == 1 ? "" : "s", a2);
        }
        return;
    }
    if (a1 != NULL && strcmp(a1, "clear") == 0) {
        i650_deck_clear(d);
        puts("punch stacker emptied");
        return;
    }
    unsigned long n = MEMORY_LINES;
    if (a1 != NULL && !parse_count(a1, &n)) {
        puts("usage: punch [n] | punch save <file> | punch clear");
        return;
    }
    printf("punch stacker: %zu card%s\n", d->count, d->count == 1 ? "" : "s");
    if (d->count > 0) {
        display_cards(stdout, d, n);
    }
}

static void stacker(struct session *s, const char *a1) {
    struct i650_deck *d = &s->m->io.read_stacker;
    if (a1 != NULL && strcmp(a1, "clear") == 0) {
        i650_deck_clear(d);
        puts("read stacker emptied");
        return;
    }
    unsigned long n = MEMORY_LINES;
    if (a1 != NULL && !parse_count(a1, &n)) {
        puts("usage: stacker [n] | stacker clear");
        return;
    }
    printf("read stacker: %zu card%s\n", d->count, d->count == 1 ? "" : "s");
    if (d->count > 0) {
        display_cards(stdout, d, n);
    }
}

/* Returns false to quit. */
static bool execute(struct session *s, char *line) {
    char copy[LINE_MAX_LEN];
    snprintf(copy, sizeof copy, "%s", line);
    char *cmd = strtok(line, " \t");
    if (cmd == NULL) {
        if (s->repeat[0] == '\0') {
            return true;
        }
        snprintf(copy, sizeof copy, "%s", s->repeat);
        snprintf(line, LINE_MAX_LEN, "%s", s->repeat);
        cmd = strtok(line, " \t");
    }
    char *a1 = strtok(NULL, " \t");
    char *a2 = strtok(NULL, " \t");
    char *a3 = strtok(NULL, " \t");
    s->repeat[0] = '\0';
    if (strcmp(cmd, "s") != 0) {
        s->in_trace_block = false;
    }

    if (strcmp(cmd, "s") == 0) {
        unsigned long n = 1;
        if (a1 != NULL && !parse_count(a1, &n)) {
            puts("usage: s [count]");
            return true;
        }
        snprintf(s->repeat, sizeof s->repeat, "%s", copy);
        run(s, SPEED_STEP, n);
    } else if (strcmp(cmd, "r") == 0) {
        run(s, SPEED_NATIVE, 0);
    } else if (strcmp(cmd, "f") == 0) {
        run(s, SPEED_FAST, 0);
    } else if (strcmp(cmd, "w") == 0) {
        if (a1 != NULL) {
            char *end;
            double v = strtod(a1, &end);
            if (*end != '\0' || v <= 0 || v > 600) {
                puts("usage: w [seconds per revolution]");
                return true;
            }
            s->walk_seconds = v;
        }
        run(s, SPEED_WALK, 0);
    } else if (strcmp(cmd, "p") == 0) {
        display_panel(stdout, s->m, a1 != NULL && strcmp(a1, "b") == 0);
    } else if (strcmp(cmd, "m") == 0) {
        memory(s, a1, a2, a3);
        snprintf(s->repeat, sizeof s->repeat, "m");
    } else if (strcmp(cmd, "v") == 0) {
        uint16_t a = 0;
        if (a1 != NULL && (!parse_address(a1, &a) || a >= I650_DRUM_WORDS)) {
            puts("usage: v [drum address]");
            return true;
        }
        display_map(stdout, s->m, a1 != NULL ? (int)a : -1);
    } else if (strcmp(cmd, "t") == 0) {
        s->trace = !s->trace;
        printf("trace %s\n", s->trace ? "on" : "off");
    } else if (strcmp(cmd, "d") == 0) {
        s->bar = !s->bar;
        printf("timing bar %s\n", s->bar ? "on" : "off");
    } else if (strcmp(cmd, "b") == 0) {
        breakpoint(s, a1);
    } else if (strcmp(cmd, "g") == 0) {
        uint16_t a;
        if (!parse_address(a1, &a)) {
            puts("usage: g <address>");
            return true;
        }
        i650_set_address_register(s->m, a);
        printf("address register %04u\n", (unsigned)a);
    } else if (strcmp(cmd, "c") == 0) {
        i650_computer_reset(s->m);
        puts("computer reset; address register 8000");
    } else if (strcmp(cmd, "k") == 0) {
        switches(s, a1, a2);
    } else if (strcmp(cmd, "z") == 0) {
        i650_reset_counters(s->m);
        puts("counters zeroed");
    } else if (strcmp(cmd, "hopper") == 0) {
        hopper(s, copy);
    } else if (strcmp(cmd, "boot") == 0) {
        boot(s, copy);
    } else if (strcmp(cmd, "deck") == 0) {
        deck(s, a1);
    } else if (strcmp(cmd, "eof") == 0) {
        end_of_file(s);
    } else if (strcmp(cmd, "punch") == 0) {
        punch(s, a1, a2);
    } else if (strcmp(cmd, "stacker") == 0) {
        stacker(s, a1);
    } else if (strcmp(cmd, "l") == 0) {
        load(s, a1);
    } else if (strcmp(cmd, "e") == 0) {
        edit(s, a1);
    } else if (strcmp(cmd, "?") == 0 || strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
        help();
    } else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
        return false;
    } else {
        printf("unknown command '%s' (? for help)\n", cmd);
    }
    return true;
}

int repl_run(struct I650 *m, const char *path) {
    struct session *s = calloc(1, sizeof *s);
    if (s == NULL) {
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);   /* keep stdout and stderr in order */
    s->m = m;
    s->bar = true;
    s->walk_seconds = 2.0;
    s->interactive = isatty(STDIN_FILENO);
    s->tty_out = isatty(STDOUT_FILENO);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    if (s->interactive) {
        puts("IBM 650 \xc2\xb7 ? for help \xc2\xb7 q to quit");
    }
    if (path != NULL) {
        load(s, path);
    }
    char line[LINE_MAX_LEN];
    for (;;) {
        if (s->pending[0] != '\0') {
            snprintf(line, sizeof line, "%s", s->pending);
            s->pending[0] = '\0';
        } else {
            if (s->eof) {
                break;
            }
            if (s->interactive) {
                fputs("650> ", stdout);
                fflush(stdout);
            }
            if (fgets(line, sizeof line, stdin) == NULL) {
                if (s->interactive) {
                    putchar('\n');
                }
                break;
            }
            strip_newline(line);
            if (!s->interactive) {
                printf("650> %s\n", line);
            }
        }
        if (!execute(s, line)) {
            break;
        }
        fflush(stdout);
    }
    free(s);
    return 0;
}
