/* The Coupling Data Units, asserted as hardware facts.
 *
 * The AGC never reads an angle. It keeps a running total that the converter
 * nudges one count at a time, and drives an axis by loading a count and letting
 * priority control walk it to zero. Both halves go through counters and
 * involuntary sequences, so both are tested by handing the machine pulses and
 * watching what it does with its own memory cycles.
 */
#include "unity.h"

#include "test_util.h"
#include "peripherals/cdu.h"

#define CDUX_ADDR  (AGC_COUNTER_BASE + AGC_CNT_CDUX)
#define CDUXD_ADDR (AGC_COUNTER_BASE + AGC_CNT_CDUXD)

static agc *m;

void setUp(void)
{
    m = calloc(1, sizeof *m);
    agc_init(m);
    test_put_fixed(m, TEST_PROGRAM_ORIGIN, I_TCF(TEST_PROGRAM_ORIGIN));
    agc_cpu_start(m);
    m->ignore_alarms = true;
}

void tearDown(void) { free(m); m = NULL; }

/* Enable drive axes in channel 14.
 *
 * Not agc_cpu_write_channel: a channel carries write-line bits 1-14 and 16 and
 * has no bit 15 of its own (AGC4 Memo #9, WCH), so a program setting the X
 * drive — channel bit 15 in table 30-5C — writes *word* bit 16. Going straight
 * at the channel keeps the tests reading in the table's numbering rather than
 * the accumulator's. */
static void enable_drive(agc_word bits)
{
    agc_channel_write(&m->channels, AGC_CH_GYRO, bits);
}

/* Long enough for a handful of drive pulses to go out: the scaler asks for one
 * DINC per axis at 3.2 kHz, so five pulses take about 1.6 ms, or 133 MCTs.
 *
 * Waiting on the counter cell instead would not work — an erasable read is
 * destructive, so the cell reads as zero from T5 until the rewrite before T10,
 * and a poll that happens to land in that window sees a drive finish that has
 * not started. */
#define DRIVE_MCTS 500u

/* Hand over one CDU count and let priority control service it. */
static void pulse(enum agc_cdu_axis axis, bool positive)
{
    TEST_ASSERT_TRUE(agc_cdu_pulse(m, axis, positive));
    for (unsigned i = 0; i < 100u; ++i) {
        agc_tick(m);
        unsigned counter = (unsigned)AGC_CNT_CDUX + (unsigned)axis;
        if (m->cpu.counters[counter] == AGC_COUNT_NONE && m->cpu.timepulse == 1) {
            break;
        }
    }
    test_run_mcts(m, 1);
}

/* --- the angle counters ----------------------------------------------------- */

static void test_a_cdu_pulse_moves_the_angle_counter_one_count(void)
{
    pulse(AGC_CDU_X, true);
    TEST_ASSERT_EQUAL_HEX16(1, m->mem.erasable[CDUX_ADDR]);
    pulse(AGC_CDU_X, true);
    TEST_ASSERT_EQUAL_HEX16(2, m->mem.erasable[CDUX_ADDR]);
}

static void test_the_angle_counter_runs_backwards_through_minus_zero(void)
{
    /* PCDU and MCDU are ones'-complement counters, so counting down past zero
     * lands on -0 and then on -1 — the CDU's angles carry the machine's own
     * two zeroes. */
    pulse(AGC_CDU_X, false);
    TEST_ASSERT_EQUAL_HEX16(077777, m->mem.erasable[CDUX_ADDR]); /* -0 */
    pulse(AGC_CDU_X, false);
    TEST_ASSERT_EQUAL_HEX16(077776, m->mem.erasable[CDUX_ADDR]); /* -1 */
}

static void test_each_axis_has_its_own_counter(void)
{
    pulse(AGC_CDU_X, true);
    pulse(AGC_CDU_Z, true);
    pulse(AGC_CDU_Z, true);
    TEST_ASSERT_EQUAL_HEX16(1, m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_CDUX]);
    TEST_ASSERT_EQUAL_HEX16(0, m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_CDUY]);
    TEST_ASSERT_EQUAL_HEX16(2, m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_CDUZ]);
}

static void test_a_pulse_arriving_before_the_last_is_serviced_is_lost(void)
{
    /* One request cell per counter and no queue behind it. The angle is then
     * quietly wrong until the program zeroes it — a real failure mode of the
     * machine, and the reason the CDU's pulse rate is bounded by design. */
    TEST_ASSERT_TRUE(agc_cdu_pulse(m, AGC_CDU_X, true));
    TEST_ASSERT_FALSE(agc_cdu_pulse(m, AGC_CDU_X, true));
    TEST_ASSERT_EQUAL_UINT64(1, m->cdu.pulses_in[AGC_CDU_X]);
    TEST_ASSERT_EQUAL_UINT64(1, m->cdu.pulses_refused[AGC_CDU_X]);
}

/* --- the zero discretes ----------------------------------------------------- */

static void test_zeroing_the_imu_cdus_stops_them_sending(void)
{
    agc_cpu_write_channel(m, AGC_CH_IMU_CTL, AGC_CH12_ZERO_IMU_CDU);
    TEST_ASSERT_TRUE(agc_cdu_zeroed(m, AGC_CDU_X));
    TEST_ASSERT_FALSE(agc_cdu_pulse(m, AGC_CDU_X, true));
    TEST_ASSERT_EQUAL_HEX16(AGC_COUNT_NONE, m->cpu.counters[AGC_CNT_CDUX]);
}

static void test_the_imu_and_optics_cdus_are_zeroed_separately(void)
{
    /* Two discretes, two halves: the platform can be realigned while the optics
     * keep tracking. */
    agc_cpu_write_channel(m, AGC_CH_IMU_CTL, AGC_CH12_ZERO_IMU_CDU);
    TEST_ASSERT_TRUE(agc_cdu_zeroed(m, AGC_CDU_Z));
    TEST_ASSERT_FALSE(agc_cdu_zeroed(m, AGC_CDU_SHAFT));
    TEST_ASSERT_TRUE(agc_cdu_pulse(m, AGC_CDU_SHAFT, true));
}

static void test_the_zero_discrete_leaves_the_counter_in_erasable_alone(void)
{
    /* It zeroes the converter, not the computer's running total: the flight
     * software clears those itself (Luminary's ZEROICDU). A model that cleared
     * them here would hide a program that forgot to. */
    pulse(AGC_CDU_X, true);
    pulse(AGC_CDU_X, true);
    agc_cpu_write_channel(m, AGC_CH_IMU_CTL, AGC_CH12_ZERO_IMU_CDU);
    test_run_mcts(m, 20);
    TEST_ASSERT_EQUAL_HEX16(2, m->mem.erasable[CDUX_ADDR]);
}

/* --- driving an axis -------------------------------------------------------- */

static void test_a_loaded_drive_counter_walks_to_zero_emitting_pulses(void)
{
    /* The program loads a count and enables the axis; priority control runs
     * DINC once per scaler tick until the count is gone. The rate is the
     * hardware's, so the program cannot hurry it. */
    m->mem.erasable[CDUXD_ADDR] = 5;
    m->cpu.counters[AGC_CNT_CDUXD] = AGC_COUNT_UP;
    enable_drive(AGC_CH14_DRIVE_X);

    test_run_mcts(m, DRIVE_MCTS);
    /* It comes to rest on *minus* zero, not plus: MONEX puts -1 in X, so the
     * last step is 1 + (-1), and ones' complement lands that on -0. The next
     * DINC finds it with TMZ and stops. */
    TEST_ASSERT_EQUAL_HEX16(077777, m->mem.erasable[CDUXD_ADDR]);
    TEST_ASSERT_EQUAL_INT32(5, m->cdu.driven[AGC_CDU_X]);
    TEST_ASSERT_EQUAL_UINT64(5, m->cdu.drive_pulses[AGC_CDU_X]);
}

static void test_a_negative_drive_sends_minus_pulses(void)
{
    m->mem.erasable[CDUXD_ADDR] = 077774; /* -3 */
    m->cpu.counters[AGC_CNT_CDUXD] = AGC_COUNT_UP;
    enable_drive(AGC_CH14_DRIVE_X);

    test_run_mcts(m, DRIVE_MCTS);
    TEST_ASSERT_EQUAL_INT32(-3, m->cdu.driven[AGC_CDU_X]);
}

static void test_running_out_of_count_clears_the_drive_enable(void)
{
    /* ZOUT is the third thing DINC can do, and it takes the axis's own bit out
     * of channel 14 so the drive stops rather than idling at zero. */
    /* Two axes with different counts: each turns itself off when its own runs
     * out, so the short one stops while the long one is still going. A DINC
     * comes once per axis every 312.5 microseconds, so two counts is about 80
     * MCTs and six is about 190. */
    m->mem.erasable[CDUXD_ADDR] = 2;
    m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_CDUYD] = 6;
    enable_drive(AGC_CH14_DRIVE_X | AGC_CH14_DRIVE_Y);

    test_run_mcts(m, 120u);
    TEST_ASSERT_TRUE((agc_channel_read(&m->channels, AGC_CH_GYRO) & AGC_CH14_DRIVE_X) == 0);
    TEST_ASSERT_TRUE((agc_channel_read(&m->channels, AGC_CH_GYRO) & AGC_CH14_DRIVE_Y) != 0);

    test_run_mcts(m, DRIVE_MCTS);
    TEST_ASSERT_TRUE((agc_channel_read(&m->channels, AGC_CH_GYRO) & AGC_CH14_DRIVE_Y) == 0);
    TEST_ASSERT_EQUAL_INT32(2, m->cdu.driven[AGC_CDU_X]);
    TEST_ASSERT_EQUAL_INT32(6, m->cdu.driven[AGC_CDU_Y]);
}

static void test_drive_pulses_are_attributed_to_the_axis_that_asked_for_them(void)
{
    m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_CDUZD] = 4;
    m->cpu.counters[AGC_CNT_CDUZD] = AGC_COUNT_UP;
    enable_drive(AGC_CH14_DRIVE_Z);

    test_run_mcts(m, DRIVE_MCTS);
    TEST_ASSERT_EQUAL_INT32(4, m->cdu.driven[AGC_CDU_Z]);
    TEST_ASSERT_EQUAL_INT32(0, m->cdu.driven[AGC_CDU_X]);
    TEST_ASSERT_EQUAL_INT32(0, m->cdu.driven[AGC_CDU_TRUN]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_cdu_pulse_moves_the_angle_counter_one_count);
    RUN_TEST(test_the_angle_counter_runs_backwards_through_minus_zero);
    RUN_TEST(test_each_axis_has_its_own_counter);
    RUN_TEST(test_a_pulse_arriving_before_the_last_is_serviced_is_lost);
    RUN_TEST(test_zeroing_the_imu_cdus_stops_them_sending);
    RUN_TEST(test_the_imu_and_optics_cdus_are_zeroed_separately);
    RUN_TEST(test_the_zero_discrete_leaves_the_counter_in_erasable_alone);
    RUN_TEST(test_a_loaded_drive_counter_walks_to_zero_emitting_pulses);
    RUN_TEST(test_a_negative_drive_sends_minus_pulses);
    RUN_TEST(test_running_out_of_count_clears_the_drive_enable);
    RUN_TEST(test_drive_pulses_are_attributed_to_the_axis_that_asked_for_them);
    return UNITY_END();
}
