/* Golden vectors from the register diagrams in BasicOpcodes (24-5002-0).
 * Figure numbers in each test name the source. */
#include "test_util.h"
#include "../core/accum.h"

static struct i650_accum acc(uint64_t upper, uint64_t lower, bool neg) {
    struct i650_accum a;
    accum_clear(&a);
    a.upper = upper;
    a.lower = lower;
    a.neg = neg;
    return a;
}

#define ASSERT_ACC(a, u, l, n, msg)                                          \
    do {                                                                     \
        ASSERT_EQ_INT((a).upper, (u), msg);                                  \
        ASSERT_EQ_INT((a).lower, (l), msg);                                  \
        ASSERT_EQ_INT((a).neg, (n), msg);                                    \
    } while (0)

TEST(add_carries_from_lower_into_upper) {
    struct i650_accum a = acc(0, 4837910256ull, false);
    bool c;
    ASSERT_TRUE(!accum_add(&a, 0, 5411839270ull, false, &c), "no overflow");
    ASSERT_ACC(a, 1, 249749526ull, false, "Fig.3 step 2");
}

TEST(add_overflow_out_of_upper) {
    struct i650_accum a = acc(5834946763ull, 4357, false);
    bool c;
    ASSERT_TRUE(accum_add(&a, 5834946763ull, 0, false, &c), "overflow");
    ASSERT_ACC(a, 1669893526ull, 4357, false, "Fig.5 step 5");
}

TEST(subtract_through_zero_gives_complement_figure) {
    /* Fig.9 step 3: subtracting from the upper half flips the sign of the
     * whole 20-digit accumulator, so the lower half shows a complement. */
    struct i650_accum a = acc(1092459, 48371, false);
    bool c;
    accum_add(&a, 1214653, 0, true, &c);
    ASSERT_ACC(a, 122193, 9999951629ull, true, "Fig.9 step 3");
    ASSERT_TRUE(c, "result was recomplemented");
}

TEST(zero_result_keeps_accumulator_sign) {
    struct i650_accum a = acc(0, 500, true);
    bool c;
    accum_add(&a, 0, 500, false, &c);
    ASSERT_ACC(a, 0, 0, true, "-500 + 500");
}

TEST(multiply_general_fig11) {
    struct i650_accum a = acc(2000000548ull, 0, false);
    unsigned adds = accum_multiply(&a, 1111111111ull, false);
    ASSERT_ACC(a, 222222283ull, 888888828ull, false, "2000000548 x 1111111111");
    ASSERT_EQ_INT(adds, 19, "\"a total of 19 additions\"");
}

TEST(multiply_fig12a) {
    struct i650_accum a = acc(8942711365ull, 0, false);
    accum_multiply(&a, 12345678, false);
    ASSERT_ACC(a, 11040383ull, 4959230470ull, false, "Fig.12A");
}

TEST(multiply_adds_lower_into_product_fig12b) {
    struct i650_accum a = acc(8942711365ull, 111, false);
    accum_multiply(&a, 12345678, false);
    ASSERT_ACC(a, 11040494ull, 4959230470ull, false, "Fig.12B");
}

TEST(multiply_incorrect_answer_fig12c) {
    /* The manual's own "incorrect answer": a big lower half overflows
     * during the loop. Reproducing it digit for digit checks the loop. */
    struct i650_accum a = acc(8942711365ull, 9999999999ull, false);
    accum_multiply(&a, 12345678, true);
    ASSERT_ACC(a, 11040382ull, 4971576148ull, true, "Fig.12C");
}

TEST(divide_fig13) {
    unsigned sum;
    struct i650_accum a = acc(0, 4682, false);
    ASSERT_TRUE(accum_divide(&a, 125, false, true, &sum), "14-1783");
    ASSERT_ACC(a, 57, 37, false, "4682 / 125");
    ASSERT_EQ_INT(sum, 10, "quotient digit sum 3+7");

    a = acc(0, 4682, false);
    accum_divide(&a, 125, true, true, &sum);
    ASSERT_ACC(a, 57, 37, true, "14-0152: quotient minus");
    ASSERT_TRUE(a.remainder_sign_valid && !a.remainder_neg, "remainder plus");

    a = acc(0, 4682, true);
    accum_divide(&a, 125, false, true, &sum);
    ASSERT_ACC(a, 57, 37, true, "14-1236: quotient minus");
    ASSERT_TRUE(a.remainder_sign_valid && a.remainder_neg, "remainder minus");

    a = acc(852, 1034010200ull, false);
    ASSERT_TRUE(!accum_divide(&a, 125, false, true, &sum), "14-1016: overflow");
    ASSERT_ACC(a, 852, 1034010200ull, false, "untouched on overflow");
}

TEST(divide_reset_upper_fig14) {
    unsigned sum;
    struct i650_accum a = acc(0, 4682, true);
    accum_divide(&a, 125, false, false, &sum);
    ASSERT_ACC(a, 0, 37, true, "64-1236");
    ASSERT_TRUE(!a.remainder_sign_valid, "no remainder sign");
}

TEST(divide_by_zero_is_overflow) {
    unsigned sum;
    struct i650_accum a = acc(0, 1, false);
    ASSERT_TRUE(!accum_divide(&a, 0, false, true, &sum), "x / 0");
}

TEST(shift_right_fig20) {
    struct i650_accum a = acc(9987654321ull, 1234567899ull, true);
    accum_shift_right(&a, 2);
    ASSERT_ACC(a, 99876543ull, 2112345678ull, true, "30-1832");
    accum_shift_right(&a, 0);
    ASSERT_ACC(a, 99876543ull, 2112345678ull, true, "30-1830 no shift");
}

TEST(shift_right_round_fig21) {
    struct i650_accum a = acc(9987654321ull, 1234567897ull, false);
    accum_shift_right_round(&a, 2);
    ASSERT_ACC(a, 99876543ull, 2112345679ull, false, "31-1832");
    a = acc(9987654321ull, 1234567899ull, false);
    accum_shift_right_round(&a, 10);
    ASSERT_ACC(a, 0, 9987654321ull, false, "31-1830 = ten places");
}

TEST(shift_left_fig22) {
    struct i650_accum a = acc(9987654321ull, 1234567899ull, false);
    accum_shift_left(&a, 6);
    ASSERT_ACC(a, 4321123456ull, 7899000000ull, false, "35-0436");
}

static void check_sct(uint64_t u, uint64_t l, unsigned units, uint64_t eu, uint64_t el,
                      bool want_overflow, const char *msg) {
    struct i650_accum a = acc(u, l, false);
    unsigned shifts;
    bool ovf = accum_shift_left_count(&a, units, &shifts);
    ASSERT_ACC(a, eu, el, false, msg);
    ASSERT_EQ_INT(ovf, want_overflow, msg);
}

TEST(shift_and_count_fig23) {
    check_sct(0, 1236845709ull, 0, 1236845709ull, 10, false, "23A max shift");
    check_sct(12368ull, 4570900000ull, 0, 1236845709ull, 5, false, "23B");
    check_sct(1236845709ull, 8174332659ull, 0, 1236845709ull, 8174332600ull, false, "23C none");
    check_sct(123684579ull, 8174332659ull, 0, 1236845798ull, 1743326501ull, false, "23D one");
    check_sct(0, 123468579ull, 0, 123468579ull, 10, true, "23E overflow");
}

TEST(shift_and_count_fig24) {
    check_sct(1236ull, 8457090000ull, 6, 1236845709ull, 10, false, "24A max shift");
    check_sct(123684ull, 5709000000ull, 6, 1236845709ull, 8, false, "24B: 4 + 4");
    check_sct(1236845709ull, 8174332659ull, 6, 1236845709ull, 8174332600ull, false, "24C");
    check_sct(123684579ull, 8174332659ull, 6, 1236845798ull, 1743326505ull, false, "24D: 4 + 1");
}

int main(void) {
    RUN_TEST(add_carries_from_lower_into_upper);
    RUN_TEST(add_overflow_out_of_upper);
    RUN_TEST(subtract_through_zero_gives_complement_figure);
    RUN_TEST(zero_result_keeps_accumulator_sign);
    RUN_TEST(multiply_general_fig11);
    RUN_TEST(multiply_fig12a);
    RUN_TEST(multiply_adds_lower_into_product_fig12b);
    RUN_TEST(multiply_incorrect_answer_fig12c);
    RUN_TEST(divide_fig13);
    RUN_TEST(divide_reset_upper_fig14);
    RUN_TEST(divide_by_zero_is_overflow);
    RUN_TEST(shift_right_fig20);
    RUN_TEST(shift_right_round_fig21);
    RUN_TEST(shift_left_fig22);
    RUN_TEST(shift_and_count_fig23);
    RUN_TEST(shift_and_count_fig24);
    return test_util_report();
}
