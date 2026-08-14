/* Downlink, outlink and radar, asserted as hardware facts.
 *
 * Three serial paths that are easy to conflate because they share the SHINC
 * machinery, and that behave quite differently: one does not shift at all, one
 * shifts out and interrupts nobody, and one shifts in and interrupts.
 */
#include "unity.h"

#include "test_util.h"
#include "peripherals/telemetry.h"

#define RNRAD_ADDR (AGC_COUNTER_BASE + AGC_CNT_RNRAD)
#define OTLNK_ADDR (AGC_COUNTER_BASE + AGC_CNT_OTLNK)

static agc *m;

void setUp(void)
{
    m = calloc(1, sizeof *m);
    agc_init(m);
    test_put_fixed(m, TEST_PROGRAM_ORIGIN, I_TCF(TEST_PROGRAM_ORIGIN));
    agc_cpu_start(m);
    m->ignore_alarms = true;
    m->ignore_interrupts = true; /* so a raised request stays visible */
}

void tearDown(void) { free(m); m = NULL; }

static void set_ch13(agc_word bits)
{
    agc_channel_write(&m->channels, AGC_CH_MISC, bits);
}

/* --- the downlink ----------------------------------------------------------- */

static void test_nothing_downlinks_with_no_equipment_attached(void)
{
    /* A machine on a bench has no telemetry ground station, and DOWNRUPT is the
     * station asking for a word. So it never comes. */
    test_run_mcts(m, 5000);
    TEST_ASSERT_EQUAL_UINT64(0, m->telemetry.downlink_words);
    TEST_ASSERT_FALSE(m->cpu.interrupts[AGC_RUPT_DOWNRUPT]);
}

static void test_the_converter_takes_channels_34_and_35_and_asks_for_more(void)
{
    /* The downlink does not shift: the converter reads both channels whole and
     * serialises them itself, and DOWNRUPT is it asking for the next word —
     * which is why the interrupt comes after the read, not before. */
    agc_channel_write(&m->channels, 034u, 012345u);
    agc_channel_write(&m->channels, 035u, 054321u);
    agc_downlink_take_word(m);

    TEST_ASSERT_EQUAL_HEX16(012345u, m->telemetry.downlink_last_34);
    TEST_ASSERT_EQUAL_HEX16(054321u, m->telemetry.downlink_last_35);
    TEST_ASSERT_EQUAL_UINT64(1, m->telemetry.downlink_words);
    TEST_ASSERT_TRUE(m->cpu.interrupts[AGC_RUPT_DOWNRUPT]);
}

static void test_the_word_rate_is_the_ground_stations_not_the_computers(void)
{
    /* 50 words a second is one word every 20 480 timing pulses. A tenth of a
     * second is 102 400 of them, which is not a whole number of MCTs — 8533
     * MCTs is four pulses short of the fifth word — so run one MCT more. */
    agc_downlink_set_rate(m, AGC_DOWNLINK_LOW_RATE_HZ);
    test_run_mcts(m, (unsigned)(AGC_TIMEPULSE_CLOCK_HZ / 10u / AGC_TIMEPULSES_PER_MCT) + 1u);
    TEST_ASSERT_EQUAL_UINT64(5, m->telemetry.downlink_words);
}

/* --- the outlink ------------------------------------------------------------ */

static void test_the_outlink_transmits_every_bit_that_leaves_the_counter(void)
{
    /* A shift *out*: the crosslink equipment pulls a bit at a time, and both
     * ones and zeroes are transmitted — unlike the shift-in paths, where only
     * the flag bit means anything. */
    m->mem.erasable[OTLNK_ADDR] = 052525u;
    for (unsigned i = 0; i < 16u; ++i) {
        m->cpu.counters[AGC_CNT_OTLNK] = AGC_COUNT_DOWN;
        for (unsigned t = 0; t < 100u; ++t) {
            agc_tick(m);
            if (m->cpu.counters[AGC_CNT_OTLNK] == AGC_COUNT_NONE
                && m->cpu.timepulse == 1) {
                break;
            }
        }
        test_run_mcts(m, 1);
    }
    TEST_ASSERT_EQUAL_UINT64(16, m->telemetry.outlink_bits);
}

static void test_the_outlink_raises_no_interrupt(void)
{
    /* DOWNRUPT belongs to the Downlink Converter, not to this counter. Wiring
     * the two together would have the computer interrupted by its own
     * crosslink. */
    m->mem.erasable[OTLNK_ADDR] = 077777u;
    m->cpu.counters[AGC_CNT_OTLNK] = AGC_COUNT_DOWN;
    test_run_mcts(m, 20);
    TEST_ASSERT_TRUE(m->telemetry.outlink_bits > 0);
    TEST_ASSERT_FALSE(m->cpu.interrupts[AGC_RUPT_DOWNRUPT]);
}

/* --- the radar -------------------------------------------------------------- */

static void test_channel_13_selects_which_radar_answers(void)
{
    set_ch13(AGC_CH13_RADAR_SEL_C);
    TEST_ASSERT_EQUAL_INT(AGC_RADAR_RR_RANGE, agc_radar_selected(m));
    set_ch13(AGC_CH13_RADAR_SEL_B);
    TEST_ASSERT_EQUAL_INT(AGC_RADAR_RR_RANGE_RATE, agc_radar_selected(m));
    set_ch13(AGC_CH13_RADAR_SEL_A | AGC_CH13_RADAR_SEL_B | AGC_CH13_RADAR_SEL_C);
    TEST_ASSERT_EQUAL_INT(AGC_RADAR_LR_RANGE, agc_radar_selected(m));
}

static void test_two_of_the_eight_selections_are_deliberately_nothing(void)
{
    /* 000 and 011 both mean "none" in table 30-5B, so a program can select no
     * radar without clearing the whole channel. */
    set_ch13(0);
    TEST_ASSERT_EQUAL_INT(AGC_RADAR_NONE, agc_radar_selected(m));
    set_ch13(AGC_CH13_RADAR_SEL_B | AGC_CH13_RADAR_SEL_C);
    TEST_ASSERT_EQUAL_INT(AGC_RADAR_NONE, agc_radar_selected(m));
}

static void test_a_radar_word_assembles_in_rnrad_and_raises_radarrupt(void)
{
    /* The same flag-and-fifteen-bits protocol as the uplink, into a different
     * counter and a different interrupt. */
    agc_radar_send(m, 025252u);
    test_run_mcts(m, 400);

    TEST_ASSERT_FALSE(agc_radar_busy(&m->telemetry));
    TEST_ASSERT_EQUAL_HEX16(025252u, m->mem.erasable[RNRAD_ADDR]);
    TEST_ASSERT_TRUE(m->cpu.interrupts[AGC_RUPT_RADARRUPT]);
    TEST_ASSERT_FALSE(m->cpu.interrupts[AGC_RUPT_UPRUPT]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nothing_downlinks_with_no_equipment_attached);
    RUN_TEST(test_the_converter_takes_channels_34_and_35_and_asks_for_more);
    RUN_TEST(test_the_word_rate_is_the_ground_stations_not_the_computers);
    RUN_TEST(test_the_outlink_transmits_every_bit_that_leaves_the_counter);
    RUN_TEST(test_the_outlink_raises_no_interrupt);
    RUN_TEST(test_channel_13_selects_which_radar_answers);
    RUN_TEST(test_two_of_the_eight_selections_are_deliberately_nothing);
    RUN_TEST(test_a_radar_word_assembles_in_rnrad_and_raises_radarrupt);
    return UNITY_END();
}
