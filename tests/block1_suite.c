/* Block I instruction behaviour, asserted as hardware facts.
 *
 * The same discipline as the Block II suites — lay a short program in
 * fixed-fixed memory, park it, run whole memory cycles, assert on state, and
 * never reach inside an instruction — against a genuinely different machine.
 *
 * What is *not* the same is how much these tests are worth, and it is worth
 * being plain about it. The Block II suites are backed by a published memo,
 * gate netlists and MIT's own validation rope. There is no Block I memo in
 * `docs/references/`, no Block I gate model in `ext/`, and the Validation rope
 * is a Block II program. These check that our transcription of
 * `ext/agcplusplus`'s Block I model behaves as that model describes, and that
 * the machine is internally consistent — no more than that.
 */
#include "unity.h"

#include <stdlib.h>

#include "agc1.h"

/* Block I's fixed-fixed runs 2000-5777 and GOJAM lands at 2030. */
#define ORIGIN 02030u

static agc1 *m;

void setUp(void) { m = calloc(1, sizeof *m); agc1_init(m); }
void tearDown(void) { free(m); m = NULL; }

/* Block I order codes: four bits of B, so the address is the low twelve. */
#define I_TC(a)    (unsigned)(000000u | ((a) & 07777u))
#define I_CCS(a)   (unsigned)(010000u | ((a) & 07777u))
#define I_NDX(a)   (unsigned)(020000u | ((a) & 07777u))
#define I_XCH(a)   (unsigned)(030000u | ((a) & 07777u))
#define I_CS(a)    (unsigned)(0140000u | ((a) & 07777u))
#define I_TS(a)    (unsigned)(0150000u | ((a) & 07777u))
#define I_AD(a)    (unsigned)(0160000u | ((a) & 07777u))
#define I_MSK(a)   (unsigned)(0170000u | ((a) & 07777u))

/* Fixed memory is written through the array: the rope has no write path. */
static void put(unsigned addr, unsigned word)
{
    m->fixed[addr - AGC1_FIXED_FIXED_START] = agc1_w(word);
}

static void load(const unsigned *code, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        put(ORIGIN + i, code[i]);
    }
    /* Park in a transfer to itself. */
    put(ORIGIN + count, I_TC(ORIGIN + count));
}

/* --- the machine runs at all ------------------------------------------------ */

static void test_a_block_one_machine_starts_at_2030_in_stage_two(void)
{
    /* Block I's GOJAM is not Block II's: 2030 rather than 4000, stage 2 rather
     * than 1, and bank 1 selected. */
    agc1_start(m);
    TEST_ASSERT_EQUAL_HEX16(02030u, m->z);
    TEST_ASSERT_EQUAL_UINT8(2, m->st);
    TEST_ASSERT_EQUAL_HEX16(1, m->bank);
}

static void test_a_memory_cycle_is_twelve_timing_pulses(void)
{
    agc1_start(m);
    const uint8_t first = m->timepulse;
    agc1_tick_mct(m);
    TEST_ASSERT_EQUAL_UINT8(first, m->timepulse);
    TEST_ASSERT_EQUAL_UINT64(AGC1_TIMEPULSES_PER_MCT, m->timepulses);
}

/* --- instructions ----------------------------------------------------------- */

static void test_xch_swaps_the_accumulator_with_erasable(void)
{
    const unsigned code[] = { I_XCH(0100) };
    load(code, 1);
    m->erasable[0100] = 01234u;
    agc1_start(m);
    m->a = 0777u;
    for (unsigned i = 0; i < 20; ++i) {
        agc1_tick_mct(m);
    }
    TEST_ASSERT_EQUAL_HEX16(01234u, m->a);
    TEST_ASSERT_EQUAL_HEX16(0777u, m->erasable[0100]);
}

static void test_ts_stores_the_accumulator(void)
{
    const unsigned code[] = { I_TS(0101) };
    load(code, 1);
    agc1_start(m);
    m->a = 07654u;
    for (unsigned i = 0; i < 20; ++i) {
        agc1_tick_mct(m);
    }
    TEST_ASSERT_EQUAL_HEX16(07654u, m->erasable[0101]);
}

static void test_ad_adds_erasable_to_the_accumulator(void)
{
    const unsigned code[] = { I_AD(0102) };
    load(code, 1);
    agc1_start(m);
    m->erasable[0102] = 034u;
    m->a = 012u;
    for (unsigned i = 0; i < 20; ++i) {
        agc1_tick_mct(m);
    }
    TEST_ASSERT_EQUAL_HEX16(046u, m->a);
}

static void test_adding_a_negative_operand_uses_the_end_around_carry(void)
{
    /* Ones' complement, exactly as on Block II: 5 + (-3) is 2 only if the carry
     * out of bit 16 comes back in at bit 1. */
    const unsigned code[] = { I_AD(0103) };
    load(code, 1);
    agc1_start(m);
    m->erasable[0103] = 0177774u; /* -3 */
    m->a = 5u;
    for (unsigned i = 0; i < 20; ++i) {
        agc1_tick_mct(m);
    }
    TEST_ASSERT_EQUAL_HEX16(2u, m->a);
}

static void test_cs_loads_the_complement(void)
{
    const unsigned code[] = { I_CS(0104) };
    load(code, 1);
    agc1_start(m);
    m->erasable[0104] = 012345u;
    for (unsigned i = 0; i < 20; ++i) {
        agc1_tick_mct(m);
    }
    TEST_ASSERT_EQUAL_HEX16(agc1_w(~012345u), m->a);
}

static void test_mask_ands_the_operand_in(void)
{
    const unsigned code[] = { I_MSK(0105) };
    load(code, 1);
    agc1_start(m);
    /* Bit 15 of an erasable word is parity, not data: the read strips it and
     * puts the sign back from bit 16. So the operand is chosen without it,
     * rather than the expectation being bent to match. */
    m->erasable[0105] = 007707u;
    m->a = 052525u;
    for (unsigned i = 0; i < 20; ++i) {
        agc1_tick_mct(m);
    }
    TEST_ASSERT_EQUAL_HEX16(agc1_w(007707u & 052525u), m->a);
}

static void test_tc_leaves_the_return_address_in_q(void)
{
    /* Read Q as soon as the transfer has happened. Block I has no TCF, so the
     * only way to park is a TC to itself — which writes Q every time round and
     * would bury the answer under the parking address. */
    const unsigned code[] = { I_TC(02100) };
    load(code, 1);
    put(02100, I_TC(02100));
    agc1_start(m);
    for (unsigned i = 0; i < 12 && m->z < 02100u; ++i) {
        agc1_tick_mct(m);
    }
    /* Z holds the *next* word to fetch, so it is already past the destination
     * by the time the transfer has taken effect. */
    TEST_ASSERT_EQUAL_HEX16(02101u, m->z);
    TEST_ASSERT_EQUAL_HEX16(ORIGIN + 1u, m->q);
}

/* --- the parts that are not Block II ---------------------------------------- */

static void test_the_bank_register_lives_in_the_central_store(void)
{
    /* Block I has no channel instructions and no EB/FB: the bank register is
     * simply address 015 of the central store. */
    agc1_start(m);
    m->s = 015u;
    m->write_bus = agc1_w(3u << 10);
    agc1_pulse_apply(m, AGC1_P_WSC);
    TEST_ASSERT_EQUAL_HEX16(3u, m->bank);

    m->write_bus = 0;
    agc1_pulse_apply(m, AGC1_P_RSC);
    TEST_ASSERT_EQUAL_HEX16(agc1_w(3u << 10), agc1_w(m->write_bus & 0176000u));
}

static void test_the_output_registers_are_central_store_addresses(void)
{
    /* Five of them at 010-014, where Block II would use a WRITE to a channel. */
    agc1_start(m);
    m->s = 011u;
    m->write_bus = 012345u;
    agc1_pulse_apply(m, AGC1_P_WSC);
    TEST_ASSERT_EQUAL_HEX16(012345u, m->out[1]);
}

static void test_writing_the_editing_registers_shifts_on_the_way_through(void)
{
    /* Block I edits in G as the word passes, selected by the address: writing
     * to 022 *is* a cycle left. There are no named shift pulses. */
    agc1_start(m);
    m->s = 022u;
    m->write_bus = 000001u;
    agc1_pulse_apply(m, AGC1_P_WG);
    TEST_ASSERT_EQUAL_HEX16(000002u, m->g);

    m->s = 021u;
    m->write_bus = 000004u;
    agc1_pulse_apply(m, AGC1_P_WG);
    TEST_ASSERT_EQUAL_HEX16(000002u, m->g);
}

static void test_a_counter_request_steals_a_memory_cycle(void)
{
    /* Priority control works as it does on Block II — the counters just live at
     * 034 upwards and there are twenty of them. */
    const unsigned code[] = { I_TC(ORIGIN) };
    load(code, 1);
    agc1_start(m);
    m->erasable[AGC1_COUNTER_BASE + AGC1_CNT_TIME1] = 5u;
    m->counters[AGC1_CNT_TIME1] = AGC1_COUNT_UP;

    for (unsigned i = 0; i < 10; ++i) {
        agc1_tick_mct(m);
    }
    TEST_ASSERT_EQUAL_HEX16(6u, m->erasable[AGC1_COUNTER_BASE + AGC1_CNT_TIME1]);
    TEST_ASSERT_EQUAL_UINT8(AGC1_COUNT_NONE, m->counters[AGC1_CNT_TIME1]);
}

static void test_every_sequence_in_the_table_is_reachable_and_woven(void)
{
    /* A cheap structural check with real value: every row of every sequence
     * must name a timing pulse in range and pulses that exist, and the table
     * must contain the two involuntary sequences priority control injects. */
    TEST_ASSERT_TRUE(agc1_subinst_count > 0);
    for (size_t i = 0; i < agc1_subinst_count; ++i) {
        const agc1_subinst *si = &agc1_subinst_table[i];
        TEST_ASSERT_NOT_NULL(si->name);
        TEST_ASSERT_TRUE(si->row_count > 0);
        for (uint8_t r = 0; r < si->row_count; ++r) {
            const agc1_pulse_row *row = &si->rows[r];
            TEST_ASSERT_TRUE(row->tp >= 1 && row->tp <= 12);
            for (unsigned p = 0; p < AGC1_MAX_PULSES_PER_TIMEPULSE; ++p) {
                TEST_ASSERT_TRUE(row->pulse[p] < AGC1_P_COUNT);
            }
        }
    }
    TEST_ASSERT_EQUAL_STRING("PINC", agc1_subinst_pinc.name);
    TEST_ASSERT_EQUAL_STRING("MINC", agc1_subinst_minc.name);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_block_one_machine_starts_at_2030_in_stage_two);
    RUN_TEST(test_a_memory_cycle_is_twelve_timing_pulses);
    RUN_TEST(test_xch_swaps_the_accumulator_with_erasable);
    RUN_TEST(test_ts_stores_the_accumulator);
    RUN_TEST(test_ad_adds_erasable_to_the_accumulator);
    RUN_TEST(test_adding_a_negative_operand_uses_the_end_around_carry);
    RUN_TEST(test_cs_loads_the_complement);
    RUN_TEST(test_mask_ands_the_operand_in);
    RUN_TEST(test_tc_leaves_the_return_address_in_q);
    RUN_TEST(test_the_bank_register_lives_in_the_central_store);
    RUN_TEST(test_the_output_registers_are_central_store_addresses);
    RUN_TEST(test_writing_the_editing_registers_shifts_on_the_way_through);
    RUN_TEST(test_a_counter_request_steals_a_memory_cycle);
    RUN_TEST(test_every_sequence_in_the_table_is_reachable_and_woven);
    return UNITY_END();
}
