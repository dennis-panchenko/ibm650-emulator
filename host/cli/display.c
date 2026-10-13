#include "display.h"
#include "cpu.h"
#include "drum.h"
#include "opcodes.h"

#include <string.h>

void display_disassemble(struct i650_word w, char out[24]) {
    const char *name = i650_mnemonic(i650_op(w));
    if (name != NULL) {
        snprintf(out, 24, "%s %04u %04u", name, (unsigned)i650_d_address(w),
                 (unsigned)i650_i_address(w));
    } else {
        snprintf(out, 24, "op %02u %04u %04u", i650_op(w), (unsigned)i650_d_address(w),
                 (unsigned)i650_i_address(w));
    }
}

void display_revs(uint64_t word_times, char out[24]) {
    snprintf(out, 24, "%llu.%02llu", (unsigned long long)(word_times / I650_WORDS_PER_BAND),
             (unsigned long long)(word_times % I650_WORDS_PER_BAND * 2));
}

size_t display_bar(const struct i650_step_record *r, char *out, size_t max) {
    size_t len = (size_t)(r->end - r->begin);
    uint64_t fetched = r->fetched > r->begin ? r->fetched : r->begin;
    size_t n = 0;
    for (uint64_t t = r->begin; t < r->end && n < max; t++, n++) {
        char c = '=';
        if (t < fetched) {
            c = '.';
        } else if (t == fetched) {
            c = 'I';
        } else if (r->has_data && t >= r->data_ready && t < r->data_time) {
            c = '.';
        } else if (r->has_data && t == r->data_time) {
            c = 'D';
        }
        out[n] = c;
    }
    out[n] = '\0';
    return len;
}

void display_trace_prefix(const struct I650 *m, const struct i650_step_record *r,
                          char *out, size_t size) {
    char revs[24], text[24];
    display_revs(r->begin - m->counters.epoch, revs);
    display_disassemble(r->instruction, text);
    snprintf(out, size, "%9s  %04u  %-15s  ", revs, (unsigned)r->address, text);
}

void display_trace_header(FILE *f, bool bar) {
    fprintf(f, "%9s  %-4s  %-15s  ", "rev", "addr", "instruction");
    if (bar) {
        fprintf(f, "%-*s %3s", DISPLAY_BAR_MAX, ". wait   I instruction   D data   = busy", "wt");
    }
    fprintf(f, "\n");
}

void display_trace_line(FILE *f, const struct I650 *m, const struct i650_step_record *r,
                        bool bar) {
    char prefix[64];
    display_trace_prefix(m, r, prefix, sizeof prefix);
    if (!bar) {
        fprintf(f, "%s\n", prefix);
        return;
    }
    char b[DISPLAY_BAR_MAX + 1];
    size_t len = display_bar(r, b, DISPLAY_BAR_MAX);
    if (len > DISPLAY_BAR_MAX) {
        memcpy(b + DISPLAY_BAR_MAX - 3, "...", 3);
    }
    fprintf(f, "%s%-*s %3llu\n", prefix, DISPLAY_BAR_MAX, b, (unsigned long long)len);
}

void display_counters(const struct I650 *m, char *out, size_t size) {
    const struct i650_counters *c = &m->counters;
    uint64_t elapsed = m->word_time - c->epoch;
    uint64_t idle = c->idle_instruction + c->idle_data;
    char e[24], i[24];
    display_revs(elapsed, e);
    display_revs(idle, i);
    double seconds = (double)i650_word_times_to_us(elapsed) / 1e6;
    double percent = elapsed > 0 ? 100.0 * (double)idle / (double)elapsed : 0.0;
    snprintf(out, size, "%llu instr \xc2\xb7 %s rev (%.3f s) \xc2\xb7 idle %s rev (%.1f%%)",
             (unsigned long long)c->instructions, e, seconds, i, percent);
}

/* Bi-quinary lamps for each digit: 0 5 | 0 1 2 3 4, '#' lit. */
static void lamps(FILE *f, uint64_t mag, unsigned digits) {
    fprintf(f, "%18s", "");
    for (int pos = (int)digits; pos >= 1; pos--) {
        unsigned d = i650_digit(mag, (unsigned)pos);
        char l[] = "..|.....";
        l[d >= 5 ? 1 : 0] = '#';
        l[3 + d % 5] = '#';
        fprintf(f, "%s ", l);
    }
    fprintf(f, "\n");
}

static void panel_word(FILE *f, const char *label, struct i650_word w, bool biquinary) {
    char text[12];
    i650_format_word(w, text);
    fprintf(f, "%-18s%s\n", label, text);
    if (biquinary) {
        lamps(f, w.mag, 10);
    }
}

void display_panel(FILE *f, const struct I650 *m, bool biquinary) {
    char text[12], dis[24], counters[128];
    if (biquinary) {
        fprintf(f, "%-18sbi-quinary lamps per digit: 0 5 | 0 1 2 3 4, # lit\n", "");
    }
    i650_format_word(m->program_register, text);
    display_disassemble(m->program_register, dis);
    fprintf(f, "%-18s%s  %s  (from %04u)\n", "program register", text, dis,
            (unsigned)m->instruction_address);
    if (biquinary) {
        lamps(f, m->program_register.mag, 10);
    }
    fprintf(f, "%-18s%04u\n", "address register", (unsigned)m->address_register);
    panel_word(f, "distributor", m->distributor, biquinary);
    char upper[12], lower[12];
    i650_format_word(i650_word_make(m->acc.upper, m->acc.neg), upper);
    i650_format_word(i650_word_make(m->acc.lower, m->acc.neg), lower);
    upper[10] = '\0';
    fprintf(f, "%-18s%s %s   (8003 | 8002)\n", "accumulator", upper, lower);
    if (m->acc.remainder_sign_valid) {
        fprintf(f, "%-18s%c\n", "remainder sign", m->acc.remainder_neg ? '-' : '+');
    }
    if (biquinary) {
        lamps(f, m->acc.upper, 10);
        lamps(f, m->acc.lower, 10);
    }
    fprintf(f, "%-18s%s\n", "overflow", m->overflow ? "ON" : "off");
    i650_format_word(m->storage_entry, text);
    fprintf(f, "%-18sentry %s \xc2\xb7 programmed %s \xc2\xb7 overflow %s\n", "switches", text,
            m->programmed_stop ? "STOP" : "RUN", m->overflow_stop ? "STOP" : "SENSE");
    fprintf(f, "%-18s%s\n", "status",
            m->status == I650_RUNNING ? "ready" : i650_status_name(m->status));
    display_counters(m, counters, sizeof counters);
    char di[24], dd[24];
    display_revs(m->counters.idle_instruction, di);
    display_revs(m->counters.idle_data, dd);
    fprintf(f, "%-18sposition %02llu \xc2\xb7 %s\n", "drum",
            (unsigned long long)(m->word_time % I650_WORDS_PER_BAND), counters);
    fprintf(f, "%-18swaiting for instructions %s rev, for data %s rev\n", "", di, dd);
}

void display_memory_line(FILE *f, const struct I650 *m, uint16_t address, bool breakpoint) {
    struct i650_word w = i650_peek(m, address);
    char text[12], dis[24];
    i650_format_word(w, text);
    char mark = address == m->address_register ? '>' : ' ';
    fprintf(f, "%c%c%04u  %s", breakpoint ? '*' : ' ', mark, (unsigned)address, text);
    /* Words are just words; show an instruction reading only where one is
     * plausible, so data doesn't come out as "NOP 0000 0025". */
    bool executed = address < I650_DRUM_WORDS && (m->touched[address] & I650_TOUCH_EXECUTED);
    bool plausible = i650_op(w) != 0 && i650_mnemonic(i650_op(w)) != NULL &&
                     i650_valid_address(i650_d_address(w)) &&
                     i650_valid_address(i650_i_address(w));
    if (w.mag != 0 && (executed || plausible)) {
        display_disassemble(w, dis);
        fprintf(f, "  %s", dis);
    }
    fprintf(f, "\n");
}

static char map_cell(const struct I650 *m, unsigned a) {
    uint8_t t = m->touched[a];
    if (t & I650_TOUCH_EXECUTED) {
        return 'i';
    }
    if (t & I650_TOUCH_WRITTEN) {
        return 'w';
    }
    if (t & I650_TOUCH_READ) {
        return 'r';
    }
    return m->drum[a].mag != 0 || m->drum[a].neg ? ':' : '.';
}

void display_map(FILE *f, const struct I650 *m, int only) {
    fprintf(f, "      ");
    for (unsigned p = 0; p < I650_WORDS_PER_BAND; p++) {
        fputc(p % 10 == 0 ? (char)('0' + p / 10) : ' ', f);
    }
    fprintf(f, "\n      ");
    for (unsigned p = 0; p < I650_WORDS_PER_BAND; p++) {
        fputc((char)('0' + p % 10), f);
    }
    fprintf(f, "\n");
    unsigned shown = 0;
    for (unsigned band = 0; band < I650_DRUM_WORDS; band += I650_WORDS_PER_BAND) {
        char row[I650_WORDS_PER_BAND + 1];
        bool used = false;
        for (unsigned p = 0; p < I650_WORDS_PER_BAND; p++) {
            row[p] = map_cell(m, band + p);
            used |= row[p] != '.';
        }
        row[I650_WORDS_PER_BAND] = '\0';
        bool want = only >= 0 ? (unsigned)only / I650_WORDS_PER_BAND == band / I650_WORDS_PER_BAND
                              : used;
        if (want) {
            fprintf(f, "%04u  %s\n", band, row);
            shown++;
        }
    }
    if (shown == 0) {
        fprintf(f, "      (drum empty)\n");
    }
    fprintf(f, "      i executed  w written  r read  : other non-zero  . zero\n");
}
