// Host tests for the calculator state machine (calc_engine.c).
// Each test calls calc_reset() to start from a clean state.

#include "test.h"
#include "calc_engine.h"
#include <string.h>

static void test_basic_add(int *fails) {
    calc_reset();
    calc_digit(5); calc_op(OP_ADD); calc_digit(3); calc_eq();
    EXPECT_STREQ(calc_display(), "8");
}

static void test_basic_sub(int *fails) {
    calc_reset();
    calc_digit(1); calc_digit(0); calc_op(OP_SUB); calc_digit(4); calc_eq();
    EXPECT_STREQ(calc_display(), "6");
}

static void test_basic_mul(int *fails) {
    calc_reset();
    calc_digit(3); calc_op(OP_MUL); calc_digit(7); calc_eq();
    EXPECT_STREQ(calc_display(), "21");
}

static void test_basic_div(int *fails) {
    calc_reset();
    calc_digit(1); calc_digit(0); calc_op(OP_DIV); calc_digit(4); calc_eq();
    EXPECT_STREQ(calc_display(), "2.5");
}

static void test_chained_eq(int *fails) {
    // 5 + 3 = 8, = → 11, = → 14 (repeats +3 each time)
    calc_reset();
    calc_digit(5); calc_op(OP_ADD); calc_digit(3); calc_eq();
    EXPECT_STREQ(calc_display(), "8");
    calc_eq();
    EXPECT_STREQ(calc_display(), "11");
    calc_eq();
    EXPECT_STREQ(calc_display(), "14");
}

static void test_operator_chaining_left_to_right(int *fails) {
    // 5 + 3 then × 2: first evaluates 5+3=8, then 8×2=16 (no precedence)
    calc_reset();
    calc_digit(5); calc_op(OP_ADD); calc_digit(3);
    calc_op(OP_MUL);
    calc_digit(2); calc_eq();
    EXPECT_STREQ(calc_display(), "16");
}

static void test_digit_after_eq_starts_fresh(int *fails) {
    // 5 + 3 = 8, then typing 4 starts a fresh expression → 4 + 2 = 6
    calc_reset();
    calc_digit(5); calc_op(OP_ADD); calc_digit(3); calc_eq();
    calc_digit(4); calc_op(OP_ADD); calc_digit(2); calc_eq();
    EXPECT_STREQ(calc_display(), "6");
}

static void test_op_after_eq_chains_on_result(int *fails) {
    // 5 + 3 = 8, then + 2 = 10 (operates on the result)
    calc_reset();
    calc_digit(5); calc_op(OP_ADD); calc_digit(3); calc_eq();
    calc_op(OP_ADD); calc_digit(2); calc_eq();
    EXPECT_STREQ(calc_display(), "10");
}

static void test_divide_by_zero_sets_error(int *fails) {
    calc_reset();
    calc_digit(5); calc_op(OP_DIV); calc_digit(0); calc_eq();
    EXPECT_TRUE(calc_is_error());
    EXPECT_STREQ(calc_display(), "Error");
}

static void test_clear_after_error_resets(int *fails) {
    calc_reset();
    calc_digit(5); calc_op(OP_DIV); calc_digit(0); calc_eq();
    EXPECT_TRUE(calc_is_error());
    calc_clear();
    EXPECT_FALSE(calc_is_error());
    EXPECT_STREQ(calc_display(), "0");
}

static void test_error_blocks_input(int *fails) {
    // Digits and ops are ignored while in error state
    calc_reset();
    calc_digit(1); calc_op(OP_DIV); calc_digit(0); calc_eq();
    EXPECT_TRUE(calc_is_error());
    calc_digit(9);  // should be ignored
    EXPECT_STREQ(calc_display(), "Error");
}

static void test_sign_toggle(int *fails) {
    calc_reset();
    calc_digit(5);
    calc_sign();
    EXPECT_STREQ(calc_display(), "-5");
    calc_sign();
    EXPECT_STREQ(calc_display(), "5");
}

static void test_sign_on_zero_no_change(int *fails) {
    calc_reset();
    calc_sign();
    EXPECT_STREQ(calc_display(), "0");  // -0 stays "0"
}

static void test_percent(int *fails) {
    calc_reset();
    calc_digit(5); calc_digit(0);
    calc_pct();
    EXPECT_STREQ(calc_display(), "0.5");
}

static void test_decimal_input(int *fails) {
    // 0.5 × 2 = 1
    calc_reset();
    calc_dot(); calc_digit(5); calc_op(OP_MUL); calc_digit(2); calc_eq();
    EXPECT_STREQ(calc_display(), "1");
}

static void test_leading_zero_not_doubled(int *fails) {
    calc_reset();
    calc_digit(0); calc_digit(5);
    EXPECT_STREQ(calc_display(), "5");  // "05" is invalid
}

static void test_double_clear_resets_op(int *fails) {
    // First C clears current input to "0", second C clears all state
    calc_reset();
    calc_digit(5); calc_op(OP_ADD); calc_digit(3);
    calc_clear();  // "3" → "0"; s_op still set
    EXPECT_STREQ(calc_display(), "0");
    calc_clear();  // disp already "0" → full reset
    char expr[56];
    calc_get_expr(expr, sizeof(expr));
    EXPECT_STREQ(expr, "");  // no pending operation
}

// ── Suite entry point ─────────────────────────────────────────────────────

void run_calculator_tests(int *fails)
{
    test_basic_add(fails);
    test_basic_sub(fails);
    test_basic_mul(fails);
    test_basic_div(fails);
    test_chained_eq(fails);
    test_operator_chaining_left_to_right(fails);
    test_digit_after_eq_starts_fresh(fails);
    test_op_after_eq_chains_on_result(fails);
    test_divide_by_zero_sets_error(fails);
    test_clear_after_error_resets(fails);
    test_error_blocks_input(fails);
    test_sign_toggle(fails);
    test_sign_on_zero_no_change(fails);
    test_percent(fails);
    test_decimal_input(fails);
    test_leading_zero_not_doubled(fails);
    test_double_clear_resets_op(fails);
}
