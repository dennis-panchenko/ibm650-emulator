#include "opcodes.h"

#include <ctype.h>
#include <stddef.h>

static const char *const names[100] = {
    [0] = "NOP", [1] = "HLT",
    [10] = "AUP", [11] = "SUP", [14] = "DIV", [15] = "ALO", [16] = "SLO",
    [17] = "AML", [18] = "SML", [19] = "MPY",
    [20] = "STL", [21] = "STU", [22] = "SDA", [23] = "SIA", [24] = "STD",
    [30] = "SRT", [31] = "SRD", [35] = "SLT", [36] = "SCT",
    [44] = "NZU", [45] = "NZE", [46] = "BMI", [47] = "BOV",
    [60] = "RAU", [61] = "RSU", [64] = "DVR", [65] = "RAL", [66] = "RSL",
    [67] = "RAM", [68] = "RSM", [69] = "LDD",
    [70] = "RD1", [71] = "WR1", [72] = "RC1", [73] = "RD2", [74] = "WR2",
    [75] = "RC2", [76] = "RD3", [77] = "WR3", [78] = "RC3",
    [84] = "TLU",
    [90] = "BD0", [91] = "BD1", [92] = "BD2", [93] = "BD3", [94] = "BD4",
    [95] = "BD5", [96] = "BD6", [97] = "BD7", [98] = "BD8", [99] = "BD9",
};

const char *i650_mnemonic(unsigned op) {
    return op < 100 ? names[op] : NULL;
}

int i650_opcode(const char *mnemonic) {
    for (int op = 0; op < 100; op++) {
        const char *n = names[op];
        if (n == NULL) {
            continue;
        }
        size_t k = 0;
        while (n[k] != '\0' && toupper((unsigned char)mnemonic[k]) == n[k]) {
            k++;
        }
        if (n[k] == '\0' && mnemonic[k] == '\0') {
            return op;
        }
    }
    return -1;
}
