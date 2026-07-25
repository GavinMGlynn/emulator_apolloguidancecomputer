/* Instruction behaviour, asserted as hardware facts.
 *
 * Each test lays a short program in fixed-fixed memory, parks it in a
 * branch-to-itself immediately after the last instruction, runs enough Memory
 * Cycle Times to reach that park, and asserts on machine state. Nothing here
 * reaches inside an instruction — the only interface is the timing pulse.
 *
 * The park matters: the AGC has no halt, and an unwoven word fetched by a
 * program that ran off the end would fail parity and restart the machine, so a
 * test without a park would silently be testing the second pass.
 */
#include "unity.h"

#include "test_util.h"

/* Comfortably past the longest program below, and far short of the ~853 MCTs
 * it would take an alarm to notice the parking loop. */
#define PARK_MCTS 60u

/* Constants live past the park so the program never falls into them. */
#define K0 04040u
#define K1 04041u

static agc *m;

void setUp(void) { m = calloc(1, sizeof *m); agc_init(m); }
void tearDown(void) { free(m); m = NULL; }

/* Assemble `code` at 04000 and park immediately after it. */
static void load(const unsigned *code, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        test_put_fixed(m, TEST_PROGRAM_ORIGIN + i, code[i]);
    }
    unsigned park = TEST_PROGRAM_ORIGIN + count;
    test_put_fixed(m, park, I_TCF(park));
}

static void konst(unsigned addr, unsigned value)
{
    test_put_fixed(m, addr, value);
}

static void run_to_park(void)
{
    agc_cpu_start(m);
    test_run_mcts(m, PARK_MCTS);
}

/* --- data movement ---------------------------------------------------------- */

static void test_ca_loads_the_accumulator_from_fixed_memory(void)
{
    const unsigned code[] = { I_CA(K0) };
    load(code, 1);
    konst(K0, 012345);
    run_to_park();
    TEST_ASSERT_EQUAL_HEX16(012345, m->cpu.a);
}

static void test_cs_loads_the_ones_complement_of_the_operand(void)
{
    const unsigned code[] = { I_CS(K0) };
    load(code, 1);
    konst(K0, 012345);
    run_to_park();
    TEST_ASSERT_EQUAL_HEX16(agc_w(~012345u), m->cpu.a);
}

static void test_xch_swaps_the_accumulator_with_erasable_memory(void)
{
    const unsigned code[] = { I_CA(K0), I_XCH(0100) };
    load(code, 2);
    konst(K0, 000777);
    m->mem.erasable[0100] = 001234;
    run_to_park();

    TEST_ASSERT_EQUAL_HEX16(001234, m->cpu.a);
    TEST_ASSERT_EQUAL_HEX16(000777, m->mem.erasable[0100]);
}

static void test_ts_stores_the_accumulator_into_erasable_memory(void)
{
    const unsigned code[] = { I_CA(K0), I_TS(0100) };
    load(code, 2);
    konst(K0, 007654);
    run_to_park();
    TEST_ASSERT_EQUAL_HEX16(007654, m->mem.erasable[0100]);
}

static void test_lxch_swaps_l_with_erasable_memory(void)
{
    const unsigned code[] = { I_LXCH(0100) };
    load(code, 1);
    m->mem.erasable[0100] = 004321;
    agc_cpu_start(m);
    m->cpu.l = 001111;
    test_run_mcts(m, PARK_MCTS);

    TEST_ASSERT_EQUAL_HEX16(004321, m->cpu.l);
    TEST_ASSERT_EQUAL_HEX16(001111, m->mem.erasable[0100]);
}

/* --- arithmetic ------------------------------------------------------------- */

static void test_ad_adds_the_operand_to_the_accumulator(void)
{
    const unsigned code[] = { I_CA(K0), I_AD(K1) };
    load(code, 2);
    konst(K0, 000012);
    konst(K1, 000034);
    run_to_park();
    TEST_ASSERT_EQUAL_HEX16(000046, m->cpu.a);
}

static void test_adding_a_negative_operand_uses_the_end_around_carry(void)
{
    /* 5 + (-3). -3 is 077774 in 15 bits, sign-extended to 0177774 on the way
     * out of memory. The carry out of bit 16 comes back in at bit 1 — that
     * end-around carry is what makes ones' complement arithmetic work. */
    const unsigned code[] = { I_CA(K0), I_AD(K1) };
    load(code, 2);
    konst(K0, 000005);
    konst(K1, 077774);
    run_to_park();
    TEST_ASSERT_EQUAL_HEX16(000002, m->cpu.a);
}

static void test_adding_a_value_to_its_own_negation_gives_minus_zero(void)
{
    /* Ones' complement has two zeroes and x + (-x) lands on the negative one.
     * This is why the hardware carries a dedicated TMZ pulse. */
    const unsigned code[] = { I_CA(K0), I_AD(K1) };
    load(code, 2);
    konst(K0, 000007);
    konst(K1, 077770);
    run_to_park();
    TEST_ASSERT_EQUAL_HEX16(0177777, m->cpu.a);
}

static void test_mask_ands_the_operand_into_the_accumulator(void)
{
    const unsigned code[] = { I_CA(K0), I_MASK(K1) };
    load(code, 2);
    konst(K0, 077707);
    konst(K1, 052525);
    run_to_park();
    TEST_ASSERT_EQUAL_HEX16(052505, agc_w(m->cpu.a & 077777u));
}

static void test_ads_adds_the_accumulator_into_erasable_memory(void)
{
    const unsigned code[] = { I_CA(K0), I_ADS(0100) };
    load(code, 2);
    konst(K0, 000010);
    m->mem.erasable[0100] = 000005;
    run_to_park();

    TEST_ASSERT_EQUAL_HEX16(000015, m->mem.erasable[0100]);
    TEST_ASSERT_EQUAL_HEX16(000015, agc_w(m->cpu.a & 077777u));
}

static void test_incr_adds_one_to_an_erasable_word(void)
{
    const unsigned code[] = { I_INCR(0100) };
    load(code, 1);
    m->mem.erasable[0100] = 000077;
    run_to_park();
    TEST_ASSERT_EQUAL_HEX16(000100, m->mem.erasable[0100]);
}

/* --- control flow ----------------------------------------------------------- */

static void test_tc_leaves_the_return_address_in_q(void)
{
    /* TC to the park; Q holds the address after the TC. */
    const unsigned code[] = { I_TC(04002), 0 };
    load(code, 2);
    run_to_park();
    TEST_ASSERT_EQUAL_HEX16(04001, m->cpu.q);
}

static void test_tcf_transfers_control_without_touching_q(void)
{
    /* Put a known return address in Q with a TC, then check a TCF leaves it
     * alone. Setting Q from the test would not do: the boot sequence's own TC0
     * writes Q before the first program instruction runs. */
    const unsigned code[] = { I_TC(04002), 0, I_TCF(04005) };
    load(code, 3);
    test_put_fixed(m, 04005, I_TCF(04005));
    run_to_park();

    TEST_ASSERT_EQUAL_HEX16(04001, m->cpu.q);
    /* Z runs one past the instruction being executed. */
    TEST_ASSERT_EQUAL_HEX16(04006, m->cpu.z);
}

static void test_ccs_leaves_the_diminished_absolute_value_in_the_accumulator(void)
{
    /* CCS of +7 leaves |7| - 1 = 6 in A. The four branch words must all be
     * real instructions: CCS skips *into* them, and a zero word would decode as
     * TC 0 and run away into the accumulator. */
    const unsigned code[] = { I_CCS(0100), I_TCF(04005), I_TCF(04005),
                              I_TCF(04005), I_TCF(04005) };
    load(code, 5);
    m->mem.erasable[0100] = 000007;
    run_to_park();
    TEST_ASSERT_EQUAL_HEX16(000006, agc_w(m->cpu.a & 077777u));
}

static void test_ccs_of_plus_zero_takes_the_second_of_its_four_branches(void)
{
    /* CCS's four outcomes are +, +0, -, -0, and the branch is *which of the
     * next four words is executed*, not a flag. +0 takes the second. */
    const unsigned code[] = { I_CCS(0100), I_TCF(04010), I_TCF(04011),
                              I_TCF(04012), I_TCF(04013) };
    load(code, 5);
    for (unsigned a = 04010; a <= 04013; ++a) {
        test_put_fixed(m, a, I_TCF(a));
    }
    m->mem.erasable[0100] = 0; /* +0 */
    run_to_park();

    /* Parked at 04011, so Z sits one past it. */
    TEST_ASSERT_EQUAL_HEX16(04011, agc_w(m->cpu.z - 1u));
}

static void test_ccs_of_a_positive_value_takes_the_first_branch(void)
{
    const unsigned code[] = { I_CCS(0100), I_TCF(04010), I_TCF(04011),
                              I_TCF(04012), I_TCF(04013) };
    load(code, 5);
    for (unsigned a = 04010; a <= 04013; ++a) {
        test_put_fixed(m, a, I_TCF(a));
    }
    m->mem.erasable[0100] = 000003;
    run_to_park();
    TEST_ASSERT_EQUAL_HEX16(04010, agc_w(m->cpu.z - 1u));
}

/* --- flags and pseudo-codes -------------------------------------------------- */

static void test_inhint_sets_the_interrupt_inhibit(void)
{
    const unsigned code[] = { I_INHINT };
    load(code, 1);
    run_to_park();
    TEST_ASSERT_TRUE(m->cpu.inhibit_interrupts);
}

static void test_relint_clears_the_interrupt_inhibit(void)
{
    const unsigned code[] = { I_INHINT, I_RELINT };
    load(code, 2);
    run_to_park();
    TEST_ASSERT_FALSE(m->cpu.inhibit_interrupts);
}

static void test_an_extracode_needs_the_extend_pseudo_code_before_it(void)
{
    /* EXTEND then MSU. Without the EXTEND the same word would decode as DAS. */
    const unsigned code[] = { I_CA(K0), I_EXTEND, I_MSU(0100) };
    load(code, 3);
    konst(K0, 000012);
    m->mem.erasable[0100] = 000004;
    run_to_park();

    /* MSU is the two's-complement subtract: 012 - 4 = 6. */
    TEST_ASSERT_EQUAL_HEX16(000006, agc_w(m->cpu.a & 077777u));
}

/* --- the machine as a whole -------------------------------------------------- */

static void test_a_power_on_gojam_starts_execution_at_4000(void)
{
    test_put_fixed(m, 04000, I_TCF(04000));
    agc_cpu_start(m);
    /* GOJ1, then the TC0 that fetches the word at 04000. */
    test_run_mcts(m, 2);
    TEST_ASSERT_EQUAL_HEX16(04001, m->cpu.z);
}

static void test_fetching_an_unwoven_rope_word_raises_the_parity_alarm(void)
{
    /* No rope at all: the first fetch finds even parity, which is how a
     * machine with a missing rope module announces itself. */
    agc_cpu_start(m);
    test_run_mcts(m, 4);
    TEST_ASSERT_TRUE(m->alarm_latched);
    TEST_ASSERT_TRUE(
        (agc_cpu_read_channel(m, AGC_CH_ALARMS) & AGC_ALARM_PARITY_FAIL) != 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ca_loads_the_accumulator_from_fixed_memory);
    RUN_TEST(test_cs_loads_the_ones_complement_of_the_operand);
    RUN_TEST(test_xch_swaps_the_accumulator_with_erasable_memory);
    RUN_TEST(test_ts_stores_the_accumulator_into_erasable_memory);
    RUN_TEST(test_lxch_swaps_l_with_erasable_memory);
    RUN_TEST(test_ad_adds_the_operand_to_the_accumulator);
    RUN_TEST(test_adding_a_negative_operand_uses_the_end_around_carry);
    RUN_TEST(test_adding_a_value_to_its_own_negation_gives_minus_zero);
    RUN_TEST(test_mask_ands_the_operand_into_the_accumulator);
    RUN_TEST(test_ads_adds_the_accumulator_into_erasable_memory);
    RUN_TEST(test_incr_adds_one_to_an_erasable_word);
    RUN_TEST(test_tc_leaves_the_return_address_in_q);
    RUN_TEST(test_tcf_transfers_control_without_touching_q);
    RUN_TEST(test_ccs_leaves_the_diminished_absolute_value_in_the_accumulator);
    RUN_TEST(test_ccs_of_plus_zero_takes_the_second_of_its_four_branches);
    RUN_TEST(test_ccs_of_a_positive_value_takes_the_first_branch);
    RUN_TEST(test_inhint_sets_the_interrupt_inhibit);
    RUN_TEST(test_relint_clears_the_interrupt_inhibit);
    RUN_TEST(test_an_extracode_needs_the_extend_pseudo_code_before_it);
    RUN_TEST(test_a_power_on_gojam_starts_execution_at_4000);
    RUN_TEST(test_fetching_an_unwoven_rope_word_raises_the_parity_alarm);
    return UNITY_END();
}
