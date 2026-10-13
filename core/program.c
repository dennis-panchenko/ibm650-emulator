#include "program.h"
#include "cpu.h"
#include "opcodes.h"

#include <string.h>

#define MAX_TOKENS 5
#define TOKEN_LEN 24

struct token {
    char text[TOKEN_LEN];
    unsigned column;
};

static void append(char *buf, size_t size, const char *text) {
    size_t used = strlen(buf);
    while (*text != '\0' && used + 1 < size) {
        buf[used++] = *text++;
    }
    buf[used] = '\0';
}

static bool fail(struct i650_program_error *err, unsigned line, unsigned column,
                 const char *message, const char *detail) {
    err->line = line;
    err->column = column;
    err->message[0] = '\0';
    append(err->message, sizeof err->message, message);
    if (detail != NULL) {
        append(err->message, sizeof err->message, " '");
        append(err->message, sizeof err->message, detail);
        append(err->message, sizeof err->message, "'");
    }
    return false;
}

static bool is_digits(const char *s, size_t min, size_t max) {
    size_t n = strlen(s);
    if (n < min || n > max) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

static uint64_t to_number(const char *s) {
    uint64_t v = 0;
    for (; *s != '\0'; s++) {
        v = v * 10 + (uint64_t)(*s - '0');
    }
    return v;
}

bool i650_program_parse_word(const char *s, struct i650_word *out) {
    char digits[12];
    size_t n = strlen(s);
    bool neg = false;
    if (n > 0 && (s[n - 1] == '+' || s[n - 1] == '-')) {
        neg = s[n - 1] == '-';
        n--;
    }
    if (n == 0 || n > 10) {
        return false;
    }
    memcpy(digits, s, n);
    digits[n] = '\0';
    if (!is_digits(digits, 1, 10)) {
        return false;
    }
    *out = i650_word_make(to_number(digits), neg);
    return true;
}

static bool parse_address(const char *s, uint16_t *out) {
    if (!is_digits(s, 1, 4)) {
        return false;
    }
    *out = (uint16_t)to_number(s);
    return true;
}

/* Splits one line (comment already removed) into tokens. */
static int tokenize(const char *line, size_t len, struct token *tokens, unsigned lineno,
                    struct i650_program_error *err) {
    int count = 0;
    size_t i = 0;
    while (i < len) {
        while (i < len && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) {
            i++;
        }
        if (i >= len) {
            break;
        }
        size_t start = i;
        while (i < len && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
            i++;
        }
        if (count == MAX_TOKENS) {
            fail(err, lineno, (unsigned)start + 1, "too many fields", NULL);
            return -1;
        }
        size_t n = i - start;
        if (n >= TOKEN_LEN) {
            fail(err, lineno, (unsigned)start + 1, "field too long", NULL);
            return -1;
        }
        memcpy(tokens[count].text, line + start, n);
        tokens[count].text[n] = '\0';
        tokens[count].column = (unsigned)start + 1;
        count++;
    }
    return count;
}

static bool parse_line(const char *line, size_t len, unsigned lineno,
                       struct i650_program *out, struct i650_program_error *err) {
    struct token t[MAX_TOKENS];
    int n = tokenize(line, len, t, lineno, err);
    if (n < 0) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    if (strcmp(t[0].text, "start") == 0) {
        if (n != 2) {
            return fail(err, lineno, t[0].column, "usage: start <address>", NULL);
        }
        uint16_t a;
        if (!parse_address(t[1].text, &a) || !i650_valid_address(a)) {
            return fail(err, lineno, t[1].column, "bad start address", t[1].text);
        }
        out->has_start = true;
        out->start = a;
        return true;
    }

    uint16_t at;
    if (!parse_address(t[0].text, &at) || at >= I650_DRUM_WORDS) {
        return fail(err, lineno, t[0].column, "expected a drum address 0000-1999, got",
                    t[0].text);
    }
    if (out->present[at]) {
        char prev[12];
        unsigned v = out->line_of[at];
        int k = (int)sizeof prev - 1;
        prev[k] = '\0';
        do {
            prev[--k] = (char)('0' + v % 10);
            v /= 10;
        } while (v > 0);
        fail(err, lineno, t[0].column, "address already used on line ", NULL);
        append(err->message, sizeof err->message, prev + k);
        return false;
    }

    struct i650_word w;
    if (n == 2) {
        if (!i650_program_parse_word(t[1].text, &w)) {
            return fail(err, lineno, t[1].column, "bad word (1-10 digits, optional sign)",
                        t[1].text);
        }
    } else if (n == 4) {
        int op = is_digits(t[1].text, 2, 2) ? (int)to_number(t[1].text)
                                            : i650_opcode(t[1].text);
        if (op < 0) {
            return fail(err, lineno, t[1].column, "unknown operation", t[1].text);
        }
        uint16_t d, i;
        if (!parse_address(t[2].text, &d)) {
            return fail(err, lineno, t[2].column, "bad D-address", t[2].text);
        }
        if (!parse_address(t[3].text, &i)) {
            return fail(err, lineno, t[3].column, "bad I-address", t[3].text);
        }
        w = i650_instruction((unsigned)op, d, i);
    } else {
        return fail(err, lineno, t[0].column,
                    "expected '<address> <word>' or '<address> <op> <D> <I>'", NULL);
    }
    out->words[at] = w;
    out->present[at] = true;
    out->line_of[at] = lineno;
    return true;
}

bool i650_program_parse(const char *text, struct i650_program *out,
                        struct i650_program_error *err) {
    memset(out, 0, sizeof *out);
    unsigned lineno = 1;
    const char *p = text;
    for (;;) {
        const char *eol = strchr(p, '\n');
        size_t len = eol != NULL ? (size_t)(eol - p) : strlen(p);
        const char *semi = memchr(p, ';', len);
        size_t code = semi != NULL ? (size_t)(semi - p) : len;
        if (!parse_line(p, code, lineno, out, err)) {
            return false;
        }
        if (eol == NULL) {
            return true;
        }
        p = eol + 1;
        lineno++;
    }
}

void i650_program_load(struct I650 *m, const struct i650_program *p) {
    i650_clear_drum(m);
    for (unsigned a = 0; a < I650_DRUM_WORDS; a++) {
        if (p->present[a]) {
            m->drum[a] = p->words[a];
        }
    }
    i650_computer_reset(m);
    if (p->has_start) {
        i650_set_address_register(m, p->start);
    }
    i650_reset_counters(m);
}
