/* Memory: the erasable core cycle, bank addressing, and rope parity. */
#include "unity.h"

#include "test_util.h"

static agc *m;

void setUp(void) { m = calloc(1, sizeof *m); agc_init(m); }
void tearDown(void) { free(m); m = NULL; }

static void test_an_erasable_read_destroys_the_word_it_returns(void)
{
    agc_memory_write_erasable(&m->mem, 0100, 012345);
    TEST_ASSERT_EQUAL_HEX16(012345, agc_memory_read_erasable(&m->mem, 0100));
    /* Coincident-current core reads by driving the cell to zero. */
    TEST_ASSERT_EQUAL_HEX16(0, m->mem.erasable[0100]);
}

static void test_an_erasable_read_duplicates_the_sign_into_bit_16(void)
{
    /* Core holds fifteen bits. Coming back out, bit 15 is duplicated into
     * bit 16 so the adder and the sign tests see a 16-bit signed quantity. */
    agc_memory_write_erasable(&m->mem, 0100, 040000);
    TEST_ASSERT_EQUAL_HEX16(040000, m->mem.erasable[0100]);
    TEST_ASSERT_EQUAL_HEX16(0140000, agc_memory_read_erasable(&m->mem, 0100));
}

static void test_core_stores_fifteen_bits_and_drops_bit_16(void)
{
    /* Which of bits 15 and 16 survives when they disagree is decided on the way
     * *to* core, by the machine, not here: memory just drops bit 16. See
     * test_storing_an_overflowed_word_keeps_bit_16_as_the_sign. */
    agc_memory_write_erasable(&m->mem, 0100, 0140000);
    TEST_ASSERT_EQUAL_HEX16(040000, m->mem.erasable[0100]);
}

static void test_erasable_register_7_is_hard_wired_to_plus_zero(void)
{
    agc_memory_write_erasable(&m->mem, 7, 077777);
    TEST_ASSERT_EQUAL_HEX16(0, agc_memory_read_erasable(&m->mem, 7));
}

static void test_unswitched_erasable_ignores_the_bank_register(void)
{
    /* Below 1400 the address is absolute whatever EB says. */
    TEST_ASSERT_EQUAL_HEX16(01377, agc_erasable_absolute(01377, 07 << 8));
}

static void test_switched_erasable_takes_its_high_bits_from_eb(void)
{
    /* EB holds the bank number in bits 9-11, i.e. pre-shifted to the 256-word
     * stride, so bank n starts at absolute word n * 0400. */
    TEST_ASSERT_EQUAL_HEX16(00000, agc_erasable_absolute(01400, 0u << 8));
    TEST_ASSERT_EQUAL_HEX16(01400, agc_erasable_absolute(01400, 3u << 8));
    TEST_ASSERT_EQUAL_HEX16(03777, agc_erasable_absolute(01777, 7u << 8));
}

static void test_fixed_fixed_addresses_bypass_the_bank_register(void)
{
    /* 4000-7777 are banks 2 and 3, always mapped, whatever FB holds. */
    TEST_ASSERT_EQUAL_UINT(04000, agc_fixed_absolute(04000, 076000, 0));
    TEST_ASSERT_EQUAL_UINT(07777, agc_fixed_absolute(07777, 076000, 0));
}

static void test_switched_fixed_takes_its_bank_from_fb(void)
{
    /* Bank 4 (FB = 4 << 10) offset 0 is absolute word 4 * 1024. */
    TEST_ASSERT_EQUAL_UINT(4u * 1024u, agc_fixed_absolute(02000, 04 << 10, 0));
    TEST_ASSERT_EQUAL_UINT(4u * 1024u + 01777u, agc_fixed_absolute(03777, 04 << 10, 0));
}

static void test_fext_is_ignored_unless_both_it_and_fb_select_a_superbank(void)
{
    /* Superbanking needs FEXT >= 4 *and* FB in the top eight banks. */
    TEST_ASSERT_EQUAL_UINT(4u * 1024u, agc_fixed_absolute(02000, 04 << 10, 0100));
    TEST_ASSERT_EQUAL_UINT(030u * 1024u, agc_fixed_absolute(02000, 030 << 10, 0));
}

static void test_a_superbank_fetch_reaches_past_the_first_32k(void)
{
    /* FEXT = 0100 with FB in the top eight banks selects bank set 3. */
    unsigned addr = agc_fixed_absolute(02000, 030 << 10, 0100);
    TEST_ASSERT_EQUAL_UINT(32768u, addr);
}

static void test_a_rope_word_carries_odd_parity_across_all_sixteen_stored_bits(void)
{
    test_put_fixed(m, 04000, 012345);
    TEST_ASSERT_TRUE(agc_memory_fixed_parity_ok(&m->mem, 04000));
    TEST_ASSERT_EQUAL_HEX16(012345, agc_memory_read_fixed(&m->mem, 04000));
}

static void test_an_unwoven_rope_word_fails_parity(void)
{
    /* All-zero has even parity, so a fetch from unpopulated rope alarms —
     * which is how a machine with a missing module announces itself. */
    TEST_ASSERT_FALSE(agc_memory_fixed_parity_ok(&m->mem, 05000));
}

static void test_a_flipped_rope_bit_fails_parity(void)
{
    test_put_fixed(m, 04000, 012345);
    agc_word raw = agc_memory_read_fixed_raw(&m->mem, 04000);
    agc_memory_write_fixed_raw(&m->mem, 04000, agc_w(raw ^ 1u));
    TEST_ASSERT_FALSE(agc_memory_fixed_parity_ok(&m->mem, 04000));
}

static void test_a_negative_rope_word_reads_back_with_both_sign_bits_set(void)
{
    test_put_fixed(m, 04000, 077777); /* -0 in 15 bits */
    TEST_ASSERT_EQUAL_HEX16(0177777, agc_memory_read_fixed(&m->mem, 04000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_erasable_read_destroys_the_word_it_returns);
    RUN_TEST(test_an_erasable_read_duplicates_the_sign_into_bit_16);
    RUN_TEST(test_core_stores_fifteen_bits_and_drops_bit_16);
    RUN_TEST(test_erasable_register_7_is_hard_wired_to_plus_zero);
    RUN_TEST(test_unswitched_erasable_ignores_the_bank_register);
    RUN_TEST(test_switched_erasable_takes_its_high_bits_from_eb);
    RUN_TEST(test_fixed_fixed_addresses_bypass_the_bank_register);
    RUN_TEST(test_switched_fixed_takes_its_bank_from_fb);
    RUN_TEST(test_fext_is_ignored_unless_both_it_and_fb_select_a_superbank);
    RUN_TEST(test_a_superbank_fetch_reaches_past_the_first_32k);
    RUN_TEST(test_a_rope_word_carries_odd_parity_across_all_sixteen_stored_bits);
    RUN_TEST(test_an_unwoven_rope_word_fails_parity);
    RUN_TEST(test_a_flipped_rope_bit_fails_parity);
    RUN_TEST(test_a_negative_rope_word_reads_back_with_both_sign_bits_set);
    return UNITY_END();
}
