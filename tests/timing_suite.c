/* Timing: MCT structure, the scaler's clock tree, and priority control.
 *
 * These are the tests that make "cycle-correct" mean something. Every number
 * here is an AGC timing pulse count, so it must come out identical on every
 * host and build type — which is what makes the goldens portable.
 */
#include "unity.h"

#include "test_util.h"

static agc *m;

void setUp(void) { m = calloc(1, sizeof *m); agc_init(m); }
void tearDown(void) { free(m); m = NULL; }

static void test_a_memory_cycle_time_is_twelve_timing_pulses(void)
{
    TEST_ASSERT_EQUAL_UINT(1, m->cpu.timepulse);
    for (unsigned i = 0; i < 11; ++i) {
        agc_tick(m);
    }
    TEST_ASSERT_EQUAL_UINT(12, m->cpu.timepulse);
    agc_tick(m);
    TEST_ASSERT_EQUAL_UINT(1, m->cpu.timepulse);
    TEST_ASSERT_EQUAL_UINT64(AGC_TIMEPULSES_PER_MCT, m->timepulses);
}

static void test_the_scaler_advances_once_every_ten_timing_pulses(void)
{
    for (unsigned i = 0; i < AGC_SCALER_DIVISOR - 1; ++i) {
        agc_tick(m);
    }
    TEST_ASSERT_EQUAL_UINT32(0, m->scaler.state);
    agc_tick(m);
    TEST_ASSERT_EQUAL_UINT32(1, m->scaler.state);
}

/* Every scaler stage has a known period in timing pulses: stage n toggles every
 * 2^(n-1) scaler ticks, and a scaler tick is ten timing pulses. Pinning the
 * derived rates is how a mis-wired tap gets caught immediately. */
static void test_the_timer_rates_derive_from_the_documented_clock_division(void)
{
    /* Timing-pulse clock over the scaler divisor gives the stage-1 rate. */
    TEST_ASSERT_EQUAL_UINT(102400u, AGC_TIMEPULSE_CLOCK_HZ / AGC_SCALER_DIVISOR);
    /* Stage 10's two edges are TIME5 and TIME1/TIME3, 100 Hz each. */
    TEST_ASSERT_EQUAL_UINT(100u, 102400u / (1u << 10));
    /* Twelve timing pulses at 1.024 MHz is the 11.71875 us MCT. */
    TEST_ASSERT_EQUAL_UINT64(AGC_MCT_PICOSECONDS,
                             1000000000000ull * AGC_TIMEPULSES_PER_MCT
                                 / AGC_TIMEPULSE_CLOCK_HZ);
}

static void test_time1_increments_on_the_rising_edge_of_scaler_stage_10(void)
{
    m->ignore_alarms = true; /* no rope loaded; we are only watching the clock */

    /* Stage 10 rises when the scaler reaches 512. */
    uint64_t target = 512ull * AGC_SCALER_DIVISOR;
    for (uint64_t i = 0; i < target - 1; ++i) {
        agc_tick(m);
    }
    TEST_ASSERT_EQUAL_UINT32(511, m->scaler.state);

    m->cpu.counters[AGC_CNT_TIME1] = AGC_COUNT_NONE;
    m->cpu.counters[AGC_CNT_TIME3] = AGC_COUNT_NONE;
    agc_tick(m);
    TEST_ASSERT_EQUAL_UINT32(512, m->scaler.state);
    TEST_ASSERT_EQUAL_UINT(AGC_COUNT_UP, m->cpu.counters[AGC_CNT_TIME1]);
    TEST_ASSERT_EQUAL_UINT(AGC_COUNT_UP, m->cpu.counters[AGC_CNT_TIME3]);
}

static void test_time6_only_counts_while_channel_13_bit_16_is_set(void)
{
    m->ignore_alarms = true;
    /* Watch the scaler, not priority control: with counters enabled the CPU
     * would service the request and clear it before we could look. */
    m->ignore_counters = true;

    /* Stage 6 rises every 64 scaler ticks. Run past several of them with the
     * descent-throttle interrupt disarmed. */
    for (uint64_t i = 0; i < 256ull * AGC_SCALER_DIVISOR; ++i) {
        agc_tick(m);
    }
    TEST_ASSERT_EQUAL_UINT(AGC_COUNT_NONE, m->cpu.counters[AGC_CNT_TIME6]);

    agc_cpu_write_channel(m, AGC_CH_MISC, AGC_BIT(16));
    for (uint64_t i = 0; i < 128ull * AGC_SCALER_DIVISOR; ++i) {
        agc_tick(m);
    }
    TEST_ASSERT_EQUAL_UINT(AGC_COUNT_DOWN, m->cpu.counters[AGC_CNT_TIME6]);
}

/* --- priority control -------------------------------------------------------- */

static void test_a_counter_request_steals_a_memory_cycle_from_the_program(void)
{
    /* A program parked in a branch-to-self loop, plus a pending PINC on TIME4.
     * The counter must be serviced — its request cell cleared and the erasable
     * cell incremented — without the program being told anything about it. */
    test_put_fixed(m, 04000, I_TCF(04000));
    agc_cpu_start(m);
    test_run_mcts(m, 4);

    m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_TIME4] = 000041;
    m->cpu.counters[AGC_CNT_TIME4] = AGC_COUNT_UP;
    test_run_mcts(m, 4);

    TEST_ASSERT_EQUAL_UINT(AGC_COUNT_NONE, m->cpu.counters[AGC_CNT_TIME4]);
    TEST_ASSERT_EQUAL_HEX16(000042, m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_TIME4]);
}

static void test_a_counter_overflow_out_of_time1_carries_into_time2(void)
{
    /* TIME1 and TIME2 are a single 28-bit clock stitched together by the WOVR
     * pulse at the top of TIME1: overflowing TIME1 requests a TIME2 count,
     * which priority control then services in an MCT of its own. */
    test_put_fixed(m, 04000, I_TCF(04000));
    agc_cpu_start(m);
    test_run_mcts(m, 4);

    m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_TIME2] = 0;
    m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_TIME1] = 037777; /* about to wrap */
    m->cpu.counters[AGC_CNT_TIME1] = AGC_COUNT_UP;
    test_run_mcts(m, 6);

    TEST_ASSERT_EQUAL_HEX16(0, m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_TIME1]);
    TEST_ASSERT_EQUAL_HEX16(1, m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_TIME2]);
}

static void test_a_counter_overflow_out_of_time3_requests_t3rupt(void)
{
    /* Watch the request, not what happens to it: left enabled, the CPU would
     * vector to the T3RUPT handler within a couple of MCTs and KRPT would clear
     * the very cell we are asserting on. */
    m->ignore_interrupts = true;
    test_put_fixed(m, 04000, I_TCF(04000));
    agc_cpu_start(m);
    test_run_mcts(m, 4);

    m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_TIME3] = 037777;
    m->cpu.counters[AGC_CNT_TIME3] = AGC_COUNT_UP;
    test_run_mcts(m, 4);

    TEST_ASSERT_TRUE(m->cpu.interrupts[AGC_RUPT_T3RUPT]);
}

static void test_counters_are_serviced_in_address_order(void)
{
    /* Priority is wired: the lowest-numbered pending counter wins, and the read
     * of its address is what clears the request. */
    test_put_fixed(m, 04000, I_TCF(04000));
    agc_cpu_start(m);
    test_run_mcts(m, 4);

    m->cpu.counters[AGC_CNT_TIME4] = AGC_COUNT_UP;
    m->cpu.counters[AGC_CNT_TIME1] = AGC_COUNT_UP; /* lower index, higher priority */
    test_run_mcts(m, 2);

    TEST_ASSERT_EQUAL_UINT(AGC_COUNT_NONE, m->cpu.counters[AGC_CNT_TIME1]);
    TEST_ASSERT_EQUAL_UINT(AGC_COUNT_UP, m->cpu.counters[AGC_CNT_TIME4]);
}

/* --- alarms ------------------------------------------------------------------ */

/* A loop that is not pure transfer-of-control, so the TC TRAP alarm stays
 * quiet and only the night watchman can fire. It never writes erasable 067. */
static void load_non_tc_loop(agc *mm)
{
    test_put_fixed(mm, 04000, I_CA(04002));
    test_put_fixed(mm, 04001, I_TCF(04000));
    test_put_fixed(mm, 04002, 000001);
}

static void test_the_night_watchman_restarts_a_program_that_never_touches_67(void)
{
    /* Isolate the watchman. An idle machine legitimately trips RUPT LOCK first
     * — nothing has requested an interrupt in ~300 ms — so inhibit that one and
     * the TC TRAP, and leave the watchman and the parity check live. */
    m->alarm_inhibit = agc_w(AGC_ALARM_RUPT_LOCK | AGC_ALARM_TC_TRAP);
    m->ignore_interrupts = true;
    load_non_tc_loop(m);
    agc_cpu_start(m);

    /* Stage 17 falls when the scaler wraps past 2^17 ticks. */
    uint64_t ticks = (1ull << 17) * AGC_SCALER_DIVISOR + AGC_SCALER_DIVISOR;
    for (uint64_t i = 0; i < ticks; ++i) {
        agc_tick(m);
        if (m->alarm_latched) {
            break;
        }
    }

    TEST_ASSERT_TRUE(m->alarm_latched);
    TEST_ASSERT_TRUE(
        (agc_cpu_read_channel(m, AGC_CH_ALARMS) & AGC_ALARM_NIGHT_WATCH) != 0);
}

static void test_a_pure_transfer_of_control_loop_trips_the_tc_trap(void)
{
    /* The complementary case: a program that only ever executes TC/TCF can
     * never be interrupted out of its loop, so the hardware restarts it. */
    m->alarm_inhibit = agc_w(AGC_ALARM_RUPT_LOCK | AGC_ALARM_NIGHT_WATCH);
    m->ignore_interrupts = true;
    test_put_fixed(m, 04000, I_TCF(04000));
    agc_cpu_start(m);

    /* Stage 10 falls at 1024 scaler ticks. */
    uint64_t ticks = 2048ull * AGC_SCALER_DIVISOR;
    for (uint64_t i = 0; i < ticks; ++i) {
        agc_tick(m);
        if (m->alarm_latched) {
            break;
        }
    }

    TEST_ASSERT_TRUE(m->alarm_latched);
    TEST_ASSERT_TRUE(
        (agc_cpu_read_channel(m, AGC_CH_ALARMS) & AGC_ALARM_TC_TRAP) != 0);
}

static void test_an_idle_machine_trips_rupt_lock_within_about_300_milliseconds(void)
{
    /* Nothing requests an interrupt in a freshly started machine — the flight
     * software's job is to preset TIME3/TIME4 so that one arrives — so the
     * hardware concludes the interrupt machinery has jammed. */
    m->alarm_inhibit = agc_w(AGC_ALARM_TC_TRAP | AGC_ALARM_NIGHT_WATCH);
    load_non_tc_loop(m);
    agc_cpu_start(m);

    uint64_t ticks = (1ull << 14) * AGC_SCALER_DIVISOR;
    for (uint64_t i = 0; i < ticks; ++i) {
        agc_tick(m);
        if (m->alarm_latched) {
            break;
        }
    }

    TEST_ASSERT_TRUE(m->alarm_latched);
    TEST_ASSERT_TRUE(
        (agc_cpu_read_channel(m, AGC_CH_ALARMS) & AGC_ALARM_RUPT_LOCK) != 0);
}

static void test_ignore_alarms_suppresses_the_restart(void)
{
    m->ignore_alarms = true;
    m->ignore_counters = true;
    m->ignore_interrupts = true;
    load_non_tc_loop(m);
    agc_cpu_start(m);

    uint64_t ticks = (1ull << 17) * AGC_SCALER_DIVISOR + 100u;
    for (uint64_t i = 0; i < ticks; ++i) {
        agc_tick(m);
    }
    TEST_ASSERT_FALSE(m->alarm_latched);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_memory_cycle_time_is_twelve_timing_pulses);
    RUN_TEST(test_the_scaler_advances_once_every_ten_timing_pulses);
    RUN_TEST(test_the_timer_rates_derive_from_the_documented_clock_division);
    RUN_TEST(test_time1_increments_on_the_rising_edge_of_scaler_stage_10);
    RUN_TEST(test_time6_only_counts_while_channel_13_bit_16_is_set);
    RUN_TEST(test_a_counter_request_steals_a_memory_cycle_from_the_program);
    RUN_TEST(test_a_counter_overflow_out_of_time1_carries_into_time2);
    RUN_TEST(test_a_counter_overflow_out_of_time3_requests_t3rupt);
    RUN_TEST(test_counters_are_serviced_in_address_order);
    RUN_TEST(test_the_night_watchman_restarts_a_program_that_never_touches_67);
    RUN_TEST(test_a_pure_transfer_of_control_loop_trips_the_tc_trap);
    RUN_TEST(test_an_idle_machine_trips_rupt_lock_within_about_300_milliseconds);
    RUN_TEST(test_ignore_alarms_suppresses_the_restart);
    return UNITY_END();
}
