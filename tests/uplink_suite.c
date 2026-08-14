/* The serial counters and the uplink, asserted as hardware facts.
 *
 * SHINC and SHANC are the two sequences the reference model never implemented,
 * so their rows come from AGC4 Memo #9 itself. What they do is a fifteen-bit
 * left shift of a counter with a zero (SHINC) or a one (SHANC) coming in at the
 * bottom; the difference between the two sequences is a single CI at T5.
 *
 * Nothing here reaches inside a sequence. A bit is handed to the Inlink Control
 * the way the receiving equipment hands it over, and the machine is left to
 * steal its own MCTs to deal with it.
 */
#include "unity.h"

#include "test_util.h"
#include "io/uplink.h"

#define INLINK_ADDR (AGC_COUNTER_BASE + AGC_CNT_INLINK)

static agc *m;

void setUp(void)
{
    m = calloc(1, sizeof *m);
    agc_init(m);
    /* A program that does nothing but sit still, so every MCT the counters take
     * is visible and nothing else touches INLINK. */
    test_put_fixed(m, TEST_PROGRAM_ORIGIN, I_TCF(TEST_PROGRAM_ORIGIN));
    agc_cpu_start(m);
    m->ignore_alarms = true; /* a parked program trips RUPT LOCK eventually */
}

void tearDown(void) { free(m); m = NULL; }

/* Hand one bit over and let priority control service it. */
static void shift_in(bool one)
{
    TEST_ASSERT_TRUE(agc_uplink_bit(m, one));
    for (unsigned i = 0; i < 200u; ++i) {
        agc_tick(m);
        if (m->cpu.counters[AGC_CNT_INLINK] == AGC_COUNT_NONE && m->cpu.timepulse == 1) {
            break;
        }
    }
    test_run_mcts(m, 1); /* let the sequence finish writing back */
    m->uplink.since_last = AGC_UPLINK_BIT_PULSES; /* not testing the rate here */
}

/* --- the shift -------------------------------------------------------------- */

static void test_shanc_shifts_a_one_in_and_shinc_a_zero(void)
{
    shift_in(true);
    TEST_ASSERT_EQUAL_HEX16(1, m->mem.erasable[INLINK_ADDR]);
    shift_in(false);
    TEST_ASSERT_EQUAL_HEX16(2, m->mem.erasable[INLINK_ADDR]);
    shift_in(true);
    TEST_ASSERT_EQUAL_HEX16(5, m->mem.erasable[INLINK_ADDR]);
}

static void test_sixteen_bits_assemble_the_word_that_was_sent(void)
{
    /* A flag bit and fifteen data bits, most significant first. After the
     * sixteenth shift the flag has gone and the data is all that is left. */
    const unsigned data = 052525u;
    shift_in(true);
    for (int i = 14; i >= 0; --i) {
        shift_in(((data >> i) & 1u) != 0);
    }
    TEST_ASSERT_EQUAL_HEX16(data, m->mem.erasable[INLINK_ADDR]);
}

static void test_the_flag_bit_reaching_the_top_raises_uprupt(void)
{
    const unsigned data = 052525u;
    shift_in(true);
    for (int i = 14; i >= 0; --i) {
        /* The interrupt must not come early: the flag has fifteen places to
         * travel before it is shifted out. */
        TEST_ASSERT_FALSE(m->cpu.interrupts[AGC_RUPT_UPRUPT]);
        shift_in(((data >> i) & 1u) != 0);
    }
    TEST_ASSERT_TRUE(m->cpu.interrupts[AGC_RUPT_UPRUPT]);
}

static void test_a_word_with_no_flag_bit_never_interrupts(void)
{
    /* The interrupt is not counted or timed — it falls out of the shifting. A
     * ground station that forgets the flag simply never gets an UPRUPT. */
    for (unsigned i = 0; i < AGC_UPLINK_WORD_BITS; ++i) {
        shift_in(false);
    }
    TEST_ASSERT_FALSE(m->cpu.interrupts[AGC_RUPT_UPRUPT]);
}

static void test_an_arriving_word_steals_mcts_from_the_program(void)
{
    /* Priority control outranks the program, so an uplink word costs the
     * program one memory cycle per bit — cycles it never asked to give up and
     * cannot decline. Counted here by how far a parked loop gets: the TCF at
     * 04000 branches to itself once per MCT, so the number of instructions it
     * manages over a fixed span is the program's own share of the machine.
     *
     * This is the uplink's version of what the counters probe measures, and it
     * is emergent: nothing models "uplink overhead". */
    const unsigned span = AGC_UPLINK_WORD_BITS * AGC_UPLINK_BIT_PULSES;

    uint64_t quiet_start = m->timepulses;
    while (m->timepulses - quiet_start < span) {
        agc_tick(m);
    }
    uint64_t quiet_mcts = (m->timepulses - quiet_start) / AGC_TIMEPULSES_PER_MCT;

    m->ignore_interrupts = true;
    agc_uplink_send(m, 052525u);
    uint64_t busy_start = m->timepulses;
    unsigned stolen = 0;
    while (m->timepulses - busy_start < span) {
        agc_tick(m);
        /* Count only the shift-ins: the mission timers are stealing cycles in
         * this window too, which is the point of priority control but not what
         * is being measured here. */
        const char *seq = m->cpu.subinst ? m->cpu.subinst->name : "";
        if (m->cpu.timepulse == 6 && seq[0] == 'S' && seq[1] == 'H') {
            stolen++;
        }
    }

    /* Sixteen bits, sixteen stolen cycles, and the program is that much
     * shorter of them. */
    TEST_ASSERT_EQUAL_UINT(AGC_UPLINK_WORD_BITS, stolen);
    TEST_ASSERT_EQUAL_UINT64(quiet_mcts, (m->timepulses - busy_start) / AGC_TIMEPULSES_PER_MCT);
}

/* --- the Inlink Control ----------------------------------------------------- */

static void test_channel_13_bit_6_blocks_the_uplink(void)
{
    agc_cpu_write_channel(m, AGC_CH_MISC, AGC_CH13_BLOCK_INLINK);
    TEST_ASSERT_FALSE(agc_uplink_bit(m, true));
    TEST_ASSERT_EQUAL_HEX16(AGC_COUNT_NONE, m->cpu.counters[AGC_CNT_INLINK]);
}

static void test_selecting_the_crosslink_stops_the_uplink_being_heard(void)
{
    agc_cpu_write_channel(m, AGC_CH_MISC, AGC_CH13_CROSSLINK);
    TEST_ASSERT_FALSE(agc_uplink_bit(m, true));
}

static void test_the_cabin_switch_blocks_the_uplink_whatever_the_program_wants(void)
{
    m->uplink.blocked_by_switch = true;
    TEST_ASSERT_FALSE(agc_uplink_bit(m, true));
}

static void test_two_bits_too_close_together_are_dropped_and_reported(void)
{
    /* The Inlink Control can only take a bit every 156 microseconds. A second
     * one inside that window is lost, and channel 33 bit 11 says so — on an
     * inverted channel, by going to zero. */
    TEST_ASSERT_TRUE(agc_uplink_bit(m, true));
    TEST_ASSERT_FALSE(agc_uplink_bit(m, true));
    TEST_ASSERT_TRUE((agc_cpu_read_channel(m, 033u) & AGC_CH33_UPLINK_TOO_FAST) == 0);
    TEST_ASSERT_EQUAL_UINT64(1, m->uplink.bits_refused);
}

static void test_a_bit_is_accepted_again_once_the_window_has_passed(void)
{
    TEST_ASSERT_TRUE(agc_uplink_bit(m, true));
    for (unsigned i = 0; i < AGC_UPLINK_BIT_PULSES; ++i) {
        agc_tick(m);
    }
    TEST_ASSERT_TRUE(agc_uplink_bit(m, true));
}

/* --- sending a whole word --------------------------------------------------- */

static void test_a_queued_word_goes_out_at_the_hardware_rate(void)
{
    /* With interrupts live the machine would *take* the UPRUPT and clear the
     * request before this test could look at it — which is the right behaviour
     * and is why the request is latched here instead. */
    m->ignore_interrupts = true;
    agc_uplink_send(m, 052525u);
    TEST_ASSERT_TRUE(agc_uplink_busy(&m->uplink));
    /* Sixteen bits at one per 156 microseconds is about two and a half
     * milliseconds; run comfortably past that and no further. */
    for (unsigned i = 0; i < AGC_UPLINK_WORD_BITS * AGC_UPLINK_BIT_PULSES + 4000u; ++i) {
        agc_tick(m);
    }
    TEST_ASSERT_FALSE(agc_uplink_busy(&m->uplink));
    TEST_ASSERT_EQUAL_UINT64(AGC_UPLINK_WORD_BITS, m->uplink.bits_accepted);
    TEST_ASSERT_EQUAL_UINT64(0, m->uplink.bits_refused);
    TEST_ASSERT_EQUAL_HEX16(052525u, m->mem.erasable[INLINK_ADDR]);
    TEST_ASSERT_TRUE(m->cpu.interrupts[AGC_RUPT_UPRUPT]);
}

static void test_a_key_code_is_sent_three_times_over(void)
{
    /* The ground sends each five-bit code twice and then complemented; the
     * flight software takes the word apart and requires the three to agree. */
    agc_word w = agc_uplink_keycode(021u); /* VERB */
    TEST_ASSERT_EQUAL_HEX16(021u, w & 037u);
    TEST_ASSERT_EQUAL_HEX16(021u, (w >> 5) & 037u);
    TEST_ASSERT_EQUAL_HEX16(021u, ~(unsigned)(w >> 10) & 037u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_shanc_shifts_a_one_in_and_shinc_a_zero);
    RUN_TEST(test_sixteen_bits_assemble_the_word_that_was_sent);
    RUN_TEST(test_the_flag_bit_reaching_the_top_raises_uprupt);
    RUN_TEST(test_a_word_with_no_flag_bit_never_interrupts);
    RUN_TEST(test_an_arriving_word_steals_mcts_from_the_program);
    RUN_TEST(test_channel_13_bit_6_blocks_the_uplink);
    RUN_TEST(test_selecting_the_crosslink_stops_the_uplink_being_heard);
    RUN_TEST(test_the_cabin_switch_blocks_the_uplink_whatever_the_program_wants);
    RUN_TEST(test_two_bits_too_close_together_are_dropped_and_reported);
    RUN_TEST(test_a_bit_is_accepted_again_once_the_window_has_passed);
    RUN_TEST(test_a_queued_word_goes_out_at_the_hardware_rate);
    RUN_TEST(test_a_key_code_is_sent_three_times_over);
    return UNITY_END();
}
