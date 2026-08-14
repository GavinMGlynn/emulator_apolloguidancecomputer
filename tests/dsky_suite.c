/* The DSKY, asserted as hardware facts.
 *
 * Everything here goes through a real channel write from a real program where
 * it can, because the interesting part of the DSKY is not the decode table but
 * the fact that it is driven by relay words a bank at a time and holds what it
 * was last given.
 *
 * The encodings under test are not ours to choose. The relay word format and
 * the bank assignments come from Information Series #30 (table 30-5 and
 * paragraphs 30-77, 30-145A-C); the digit codes are MIT's own `RELTAB` from
 * Luminary 099; the key codes are the commented dispatch table in `CHARIN`;
 * and the flash comes from the gates (module A24 forms it off scaler stages 16
 * and 17, module A16 gates the lamps with it).
 */
#include "unity.h"

#include "test_util.h"
#include "dsky/dsky.h"

static agc *m;

void setUp(void) { m = calloc(1, sizeof *m); agc_init(m); }
void tearDown(void) { free(m); m = NULL; }

/* Write a relay word the way the program does: through the channel, which is
 * where the bit-16-to-bit-15 fixup happens. `bank` is octal, `relays` is the
 * eleven relay bits. */
static void write_relays(unsigned bank, unsigned relays)
{
    /* The channel carries write-line bits 1-14 and 16 — there is no bit 15
     * (AGC4 Memo #9, WCH) — so the top bank bit travels in bit 16. */
    unsigned word = (relays & 03777u) | ((bank & 007u) << 11);
    if (bank & 010u) {
        word |= AGC_BIT(16);
    }
    agc_cpu_write_channel(m, AGC_CH_DSKY, agc_w(word));
}

/* The relay code for a digit, from RELTAB. */
static unsigned code_of(unsigned digit)
{
    static const unsigned reltab[10] = { 025, 003, 031, 033, 017, 036, 034, 023, 035, 037 };
    return reltab[digit];
}

/* --- the display ------------------------------------------------------------ */

static void test_a_relay_word_writes_two_digits_of_one_display(void)
{
    /* Bank 12 octal is the VERB display: bits 10-6 the left digit, 5-1 the
     * right. */
    write_relays(012, (code_of(3) << 5) | code_of(7));
    TEST_ASSERT_EQUAL_UINT8(3, agc_dsky_digit(&m->dsky, AGC_DSKY_VERB1));
    TEST_ASSERT_EQUAL_UINT8(7, agc_dsky_digit(&m->dsky, AGC_DSKY_VERB2));
}

static void test_every_digit_has_the_relay_code_the_flight_software_uses(void)
{
    for (unsigned d = 0; d < 10; ++d) {
        write_relays(011, (code_of(d) << 5) | code_of(9 - d));
        TEST_ASSERT_EQUAL_UINT8(d, agc_dsky_digit(&m->dsky, AGC_DSKY_NOUN1));
        TEST_ASSERT_EQUAL_UINT8(9 - d, agc_dsky_digit(&m->dsky, AGC_DSKY_NOUN2));
    }
}

static void test_the_display_powers_up_blank_rather_than_showing_zeroes(void)
{
    /* Relay code 0 is blank and is also the power-on state of every relay, so
     * a machine that has never written the display shows nothing at all. */
    for (unsigned i = 0; i < AGC_DSKY_DIGIT_COUNT; ++i) {
        TEST_ASSERT_EQUAL_UINT8(AGC_DSKY_BLANK,
                                agc_dsky_digit(&m->dsky, (enum agc_dsky_digit)i));
    }
}

static void test_the_relays_hold_what_they_were_given(void)
{
    /* Each bank latches independently: writing one leaves the others alone.
     * This is why a program that stops half way through an update leaves half
     * a number on the panel. */
    write_relays(012, (code_of(1) << 5) | code_of(6));
    write_relays(011, (code_of(4) << 5) | code_of(0));
    TEST_ASSERT_EQUAL_UINT8(1, agc_dsky_digit(&m->dsky, AGC_DSKY_VERB1));
    TEST_ASSERT_EQUAL_UINT8(6, agc_dsky_digit(&m->dsky, AGC_DSKY_VERB2));
    TEST_ASSERT_EQUAL_UINT8(4, agc_dsky_digit(&m->dsky, AGC_DSKY_NOUN1));
    TEST_ASSERT_EQUAL_UINT8(0, agc_dsky_digit(&m->dsky, AGC_DSKY_NOUN2));
}

static void test_the_five_digits_of_r1_arrive_in_three_separate_relay_words(void)
{
    /* R1 does not divide into pairs: bank 10 octal carries D1 alone in its
     * right half, then banks 7 and 6 carry D2-D3 and D4-D5. */
    write_relays(010, code_of(1));
    write_relays(007, (code_of(2) << 5) | code_of(3));
    write_relays(006, (code_of(4) << 5) | code_of(5));
    TEST_ASSERT_EQUAL_UINT8(1, agc_dsky_digit(&m->dsky, AGC_DSKY_R1D1));
    TEST_ASSERT_EQUAL_UINT8(2, agc_dsky_digit(&m->dsky, AGC_DSKY_R1D2));
    TEST_ASSERT_EQUAL_UINT8(3, agc_dsky_digit(&m->dsky, AGC_DSKY_R1D3));
    TEST_ASSERT_EQUAL_UINT8(4, agc_dsky_digit(&m->dsky, AGC_DSKY_R1D4));
    TEST_ASSERT_EQUAL_UINT8(5, agc_dsky_digit(&m->dsky, AGC_DSKY_R1D5));
}

static void test_one_relay_word_spans_two_registers(void)
{
    /* Bank 3 octal is the seam: its left digit is the last of R2 and its right
     * digit the first of R3. */
    write_relays(003, (code_of(8) << 5) | code_of(9));
    TEST_ASSERT_EQUAL_UINT8(8, agc_dsky_digit(&m->dsky, AGC_DSKY_R2D5));
    TEST_ASSERT_EQUAL_UINT8(9, agc_dsky_digit(&m->dsky, AGC_DSKY_R3D1));
}

static void test_a_relay_code_with_no_digit_on_the_panel_reads_blank(void)
{
    write_relays(012, (001u << 5) | 002u); /* neither is a RELTAB entry */
    TEST_ASSERT_EQUAL_UINT8(AGC_DSKY_BLANK, agc_dsky_digit(&m->dsky, AGC_DSKY_VERB1));
    TEST_ASSERT_EQUAL_UINT8(AGC_DSKY_BLANK, agc_dsky_digit(&m->dsky, AGC_DSKY_VERB2));
}

static void test_the_bank_field_addresses_three_banks_that_do_not_exist(void)
{
    /* Four bits select a bank but only thirteen are wired, so codes 15-17
     * reach no relays. The Validation rope writes them. */
    write_relays(012, (code_of(5) << 5) | code_of(5));
    write_relays(015, 03777);
    write_relays(016, 03777);
    write_relays(017, 03777);
    TEST_ASSERT_EQUAL_UINT8(5, agc_dsky_digit(&m->dsky, AGC_DSKY_VERB1));
    TEST_ASSERT_EQUAL_UINT8(5, agc_dsky_digit(&m->dsky, AGC_DSKY_VERB2));
}

/* --- signs ------------------------------------------------------------------ */

static void test_the_sign_relays_ride_in_bit_11_of_two_different_banks(void)
{
    /* Each register's plus and minus relays live in bit 11 of two banks that
     * also carry digits, so a sign arrives as a side effect of writing a pair
     * of digits. R1's plus is in bank 7, its minus in bank 6. */
    write_relays(007, AGC_BIT(11));
    TEST_ASSERT_EQUAL_INT(1, agc_dsky_sign(&m->dsky, AGC_DSKY_R1));
    write_relays(007, 0);
    write_relays(006, AGC_BIT(11));
    TEST_ASSERT_EQUAL_INT(-1, agc_dsky_sign(&m->dsky, AGC_DSKY_R1));
    write_relays(006, 0);
    TEST_ASSERT_EQUAL_INT(0, agc_dsky_sign(&m->dsky, AGC_DSKY_R1));
}

static void test_both_sign_relays_set_reads_as_plus(void)
{
    /* The sign is three segments and the plus relay lights all of them, so a
     * minus underneath a plus is invisible. */
    write_relays(002, AGC_BIT(11));
    write_relays(001, AGC_BIT(11));
    TEST_ASSERT_EQUAL_INT(1, agc_dsky_sign(&m->dsky, AGC_DSKY_R3));
}

/* --- lamps and the flash ---------------------------------------------------- */

static void test_channel_11_lights_the_lamps(void)
{
    agc_cpu_write_channel(m, AGC_CH_LAMPS,
                          AGC_DSKY_LAMP_COMP_ACTY | AGC_DSKY_LAMP_UPLINK_ACTY);
    TEST_ASSERT_TRUE(agc_dsky_lamp(&m->dsky, AGC_DSKY_LAMP_COMP_ACTY));
    TEST_ASSERT_TRUE(agc_dsky_lamp(&m->dsky, AGC_DSKY_LAMP_UPLINK_ACTY));
    TEST_ASSERT_FALSE(agc_dsky_lamp(&m->dsky, AGC_DSKY_LAMP_TEMP));
}

static void test_relay_bank_14_holds_the_status_lights(void)
{
    write_relays(014, AGC_DSKY_STATUS_NO_ATT | AGC_DSKY_STATUS_GIMBAL_LOCK);
    TEST_ASSERT_TRUE((m->dsky.status & AGC_DSKY_STATUS_NO_ATT) != 0);
    TEST_ASSERT_TRUE((m->dsky.status & AGC_DSKY_STATUS_GIMBAL_LOCK) != 0);
    TEST_ASSERT_FALSE((m->dsky.status & AGC_DSKY_STATUS_AUTO) != 0);
    /* Bank 14 drives no digits. */
    TEST_ASSERT_EQUAL_UINT8(AGC_DSKY_BLANK, agc_dsky_digit(&m->dsky, AGC_DSKY_R1D1));
}

static void test_key_release_and_operator_error_are_flashed_by_the_dsky(void)
{
    /* The program only asks for the lamp; the panel blinks it. Run a whole
     * flash period and check the lamp goes off and on again without the
     * program touching anything. */
    agc_cpu_write_channel(m, AGC_CH_LAMPS, AGC_DSKY_LAMP_KEY_REL);
    bool seen_on = false, seen_off = false;
    for (unsigned i = 0; i < 4000000u && !(seen_on && seen_off); ++i) {
        agc_tick(m);
        if (agc_dsky_lamp(&m->dsky, AGC_DSKY_LAMP_KEY_REL)) {
            seen_on = true;
        } else {
            seen_off = true;
        }
    }
    TEST_ASSERT_TRUE(seen_on);
    TEST_ASSERT_TRUE(seen_off);
}

static void test_the_verb_noun_flash_runs_in_antiphase_to_the_key_release_lamp(void)
{
    /* Module A16 gate U16047 gates the VERB/NOUN flash with FLASH and the two
     * lamps with its complement, so they are never lit together. */
    agc_cpu_write_channel(m, AGC_CH_LAMPS,
                          AGC_DSKY_FLASH_ENABLE | AGC_DSKY_LAMP_KEY_REL);
    for (unsigned i = 0; i < 4000000u; ++i) {
        agc_tick(m);
        TEST_ASSERT_NOT_EQUAL(agc_dsky_lamp(&m->dsky, AGC_DSKY_LAMP_KEY_REL),
                              agc_dsky_verb_noun_visible(&m->dsky));
    }
}

static void test_the_displays_do_not_blink_unless_the_program_asks(void)
{
    agc_cpu_write_channel(m, AGC_CH_LAMPS, 0);
    for (unsigned i = 0; i < 200000u; ++i) {
        agc_tick(m);
        TEST_ASSERT_TRUE(agc_dsky_verb_noun_visible(&m->dsky));
    }
}

/* --- the keyboard ----------------------------------------------------------- */

static void test_a_keypress_puts_its_code_in_channel_15_and_asks_for_keyrupt(void)
{
    agc_dsky_press(m, AGC_KEY_VERB, 0);
    TEST_ASSERT_EQUAL_HEX16(021, agc_cpu_read_channel(m, AGC_CH_DSKY_IN));
    TEST_ASSERT_TRUE(m->cpu.interrupts[AGC_RUPT_KEYRUPT1]);
}

static void test_a_key_is_released_after_it_has_been_held(void)
{
    agc_dsky_press(m, AGC_KEY_ENTR, 120u);
    TEST_ASSERT_EQUAL_HEX16(034, agc_cpu_read_channel(m, AGC_CH_DSKY_IN));
    for (unsigned i = 0; i < 120; ++i) {
        agc_tick(m);
    }
    TEST_ASSERT_EQUAL_HEX16(0, agc_cpu_read_channel(m, AGC_CH_DSKY_IN));
}

static void test_the_mark_buttons_use_the_other_keyboard_and_the_other_interrupt(void)
{
    agc_dsky_press_nav(m, AGC_DSKY_MARK, 0);
    TEST_ASSERT_EQUAL_HEX16(AGC_DSKY_MARK, agc_cpu_read_channel(m, AGC_CH_DSKY_IN2));
    TEST_ASSERT_TRUE(m->cpu.interrupts[AGC_RUPT_KEYRUPT2]);
    TEST_ASSERT_FALSE(m->cpu.interrupts[AGC_RUPT_KEYRUPT1]);
}

/* --- what a restart does ---------------------------------------------------- */

static void test_a_restart_puts_the_lamps_out_but_leaves_the_display_standing(void)
{
    /* The lamps hang off channel 11's flip-flops, which GOJAM clears; the
     * display hangs off latching relays, which it does not. */
    write_relays(012, (code_of(3) << 5) | code_of(7));
    agc_cpu_write_channel(m, AGC_CH_LAMPS, AGC_DSKY_LAMP_COMP_ACTY);
    agc_gojam(m);
    agc_tick_mct(m);

    TEST_ASSERT_FALSE(agc_dsky_lamp(&m->dsky, AGC_DSKY_LAMP_COMP_ACTY));
    TEST_ASSERT_EQUAL_UINT8(3, agc_dsky_digit(&m->dsky, AGC_DSKY_VERB1));
    TEST_ASSERT_EQUAL_UINT8(7, agc_dsky_digit(&m->dsky, AGC_DSKY_VERB2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_relay_word_writes_two_digits_of_one_display);
    RUN_TEST(test_every_digit_has_the_relay_code_the_flight_software_uses);
    RUN_TEST(test_the_display_powers_up_blank_rather_than_showing_zeroes);
    RUN_TEST(test_the_relays_hold_what_they_were_given);
    RUN_TEST(test_the_five_digits_of_r1_arrive_in_three_separate_relay_words);
    RUN_TEST(test_one_relay_word_spans_two_registers);
    RUN_TEST(test_a_relay_code_with_no_digit_on_the_panel_reads_blank);
    RUN_TEST(test_the_bank_field_addresses_three_banks_that_do_not_exist);
    RUN_TEST(test_the_sign_relays_ride_in_bit_11_of_two_different_banks);
    RUN_TEST(test_both_sign_relays_set_reads_as_plus);
    RUN_TEST(test_channel_11_lights_the_lamps);
    RUN_TEST(test_relay_bank_14_holds_the_status_lights);
    RUN_TEST(test_key_release_and_operator_error_are_flashed_by_the_dsky);
    RUN_TEST(test_the_verb_noun_flash_runs_in_antiphase_to_the_key_release_lamp);
    RUN_TEST(test_the_displays_do_not_blink_unless_the_program_asks);
    RUN_TEST(test_a_keypress_puts_its_code_in_channel_15_and_asks_for_keyrupt);
    RUN_TEST(test_a_key_is_released_after_it_has_been_held);
    RUN_TEST(test_the_mark_buttons_use_the_other_keyboard_and_the_other_interrupt);
    RUN_TEST(test_a_restart_puts_the_lamps_out_but_leaves_the_display_standing);
    return UNITY_END();
}
