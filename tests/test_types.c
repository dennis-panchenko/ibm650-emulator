#include "test_util.h"
#include "../core/types.h"

#include <string.h>

TEST(fields_use_ibm_position_numbering) {
    uint64_t w = 3122768049ull;
    ASSERT_EQ_INT(i650_digit(w, 1), 9, "position 1 is units");
    ASSERT_EQ_INT(i650_digit(w, 10), 3, "position 10 is high-order");
    ASSERT_EQ_INT(i650_field(w, 8, 5), 2276, "positions 8-5");
    ASSERT_EQ_INT(i650_set_field(w, 8, 5, 8538), 3185388049ull, "replace 8-5 (Fig.18)");
    ASSERT_EQ_INT(i650_set_field(w, 4, 1, 1179), 3122761179ull, "replace 4-1 (Fig.19)");
}

TEST(instruction_fields) {
    struct i650_word w = i650_instruction(65, 43, 48);
    ASSERT_EQ_INT(w.mag, 6500430048ull, "op(2) D(4) I(4)");
    ASSERT_EQ_INT(i650_op(w), 65, "op");
    ASSERT_EQ_INT(i650_d_address(w), 43, "D-address");
    ASSERT_EQ_INT(i650_i_address(w), 48, "I-address");
}

TEST(valid_addresses) {
    ASSERT_TRUE(i650_valid_address(0), "0000");
    ASSERT_TRUE(i650_valid_address(1999), "1999");
    ASSERT_TRUE(!i650_valid_address(2000), "2000");
    ASSERT_TRUE(i650_valid_address(8000), "8000");
    ASSERT_TRUE(i650_valid_address(8003), "8003");
    ASSERT_TRUE(!i650_valid_address(8004), "8004");
    ASSERT_TRUE(!i650_valid_address(7999), "7999");
}

TEST(format_and_parse_round_trip) {
    char buf[12];
    i650_format_word(i650_word_make(553882, true), buf);
    ASSERT_TRUE(strcmp(buf, "0000553882-") == 0, "format");
    struct i650_word w;
    ASSERT_TRUE(i650_parse_word("0123456789+", &w), "parse ok");
    ASSERT_EQ_INT(w.mag, 123456789, "parsed magnitude");
    ASSERT_TRUE(!w.neg, "parsed sign");
    ASSERT_TRUE(!i650_parse_word("012345678+", &w), "too short");
    ASSERT_TRUE(!i650_parse_word("0123456789", &w), "no sign");
}

int main(void) {
    RUN_TEST(fields_use_ibm_position_numbering);
    RUN_TEST(instruction_fields);
    RUN_TEST(valid_addresses);
    RUN_TEST(format_and_parse_round_trip);
    return test_util_report();
}
