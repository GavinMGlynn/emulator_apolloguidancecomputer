/* The IMU, asserted as hardware facts.
 *
 * Three separate conversations, all of them pulses: torque out to the gyros,
 * velocity in from the PIPAs, and — under coarse align only — the computer
 * driving its own gimbals and watching the CDU report the movement back.
 */
#include "unity.h"

#include "test_util.h"
#include "peripherals/cdu.h"
#include "peripherals/imu.h"

#define GYROD_ADDR (AGC_COUNTER_BASE + AGC_CNT_GYROD)
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

/* Channel 14 in the table's own bit numbering: a channel has no bit 15 of its
 * own, so writing it as a program would means writing word bit 16 instead. */
static void set_ch14(agc_word bits)
{
    agc_channel_write(&m->channels, AGC_CH_GYRO, bits);
}

static void set_ch12(agc_word bits)
{
    agc_channel_write(&m->channels, AGC_CH_IMU_CTL, bits);
}

/* One DINC per axis arrives at 3.2 kHz, so a handful of pulses is well inside
 * this. */
#define DRIVE_MCTS 500u

/* --- torquing the gyros ----------------------------------------------------- */

static void test_the_gyro_count_is_the_number_of_dincs_it_took(void)
{
    /* Fine alignment: load a pulse count, pick an axis and a sign, and let the
     * hardware walk the counter down. The count is the torque. */
    m->mem.erasable[GYROD_ADDR] = 7;
    set_ch14(AGC_CH14_GYRO_ENABLE | AGC_CH14_GYRO_DRIVE | AGC_CH14_GYRO_SEL_B);
    test_run_mcts(m, DRIVE_MCTS);

    TEST_ASSERT_EQUAL_UINT64(7, m->imu.gyro_pulses[AGC_IMU_X]);
    TEST_ASSERT_EQUAL_INT32(7, m->imu.gyro_torque[AGC_IMU_X]);
    TEST_ASSERT_EQUAL_UINT64(0, m->imu.gyro_pulses[AGC_IMU_Y]);
}

static void test_the_selection_bits_choose_the_axis(void)
{
    m->mem.erasable[GYROD_ADDR] = 3;
    set_ch14(AGC_CH14_GYRO_ENABLE | AGC_CH14_GYRO_DRIVE | AGC_CH14_GYRO_SEL_A);
    test_run_mcts(m, DRIVE_MCTS);
    TEST_ASSERT_EQUAL_UINT64(3, m->imu.gyro_pulses[AGC_IMU_Y]);

    m->mem.erasable[GYROD_ADDR] = 2;
    set_ch14(AGC_CH14_GYRO_ENABLE | AGC_CH14_GYRO_DRIVE
             | AGC_CH14_GYRO_SEL_A | AGC_CH14_GYRO_SEL_B);
    test_run_mcts(m, DRIVE_MCTS);
    TEST_ASSERT_EQUAL_UINT64(2, m->imu.gyro_pulses[AGC_IMU_Z]);
}

static void test_the_third_selection_bit_is_the_sign(void)
{
    /* The pulse train carries magnitude only; which way the gyro is torqued is
     * bit 9, so the same count can mean either direction. */
    m->mem.erasable[GYROD_ADDR] = 4;
    set_ch14(AGC_CH14_GYRO_ENABLE | AGC_CH14_GYRO_DRIVE
             | AGC_CH14_GYRO_SEL_B | AGC_CH14_GYRO_SEL_C);
    test_run_mcts(m, DRIVE_MCTS);

    TEST_ASSERT_EQUAL_UINT64(4, m->imu.gyro_pulses[AGC_IMU_X]);
    TEST_ASSERT_EQUAL_INT32(-4, m->imu.gyro_torque[AGC_IMU_X]);
}

static void test_selecting_no_gyro_torques_nothing(void)
{
    /* Selection bits a and b both zero is "none" in table 30-5C, and an
     * enabled drive with nothing selected quietly does nothing — the counter
     * still empties, so the program has no way to tell from the counter. */
    m->mem.erasable[GYROD_ADDR] = 5;
    set_ch14(AGC_CH14_GYRO_ENABLE | AGC_CH14_GYRO_DRIVE);
    test_run_mcts(m, DRIVE_MCTS);

    for (unsigned a = 0; a < AGC_IMU_AXES; ++a) {
        TEST_ASSERT_EQUAL_UINT64(0, m->imu.gyro_pulses[a]);
    }
    TEST_ASSERT_TRUE(m->imu.gyro_pulses_refused > 0);
}

static void test_a_gyro_that_is_not_enabled_is_not_torqued(void)
{
    m->mem.erasable[GYROD_ADDR] = 5;
    set_ch14(AGC_CH14_GYRO_DRIVE | AGC_CH14_GYRO_SEL_B); /* no enable */
    test_run_mcts(m, DRIVE_MCTS);
    TEST_ASSERT_EQUAL_UINT64(0, m->imu.gyro_pulses[AGC_IMU_X]);
}

/* --- the PIPAs -------------------------------------------------------------- */

static void test_a_pipa_pulse_increments_its_counter(void)
{
    TEST_ASSERT_TRUE(agc_imu_accelerate(m, AGC_IMU_X, true));
    test_run_mcts(m, 4);
    TEST_ASSERT_EQUAL_HEX16(1, m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_PIPAX]);
}

static void test_deceleration_counts_the_other_way(void)
{
    TEST_ASSERT_TRUE(agc_imu_accelerate(m, AGC_IMU_Z, false));
    test_run_mcts(m, 4);
    /* MONEX puts -1 in X, so decrementing +0 gives -1 outright rather than
     * passing through minus zero: 0 + (-1) is 077776. (DINC is the sequence
     * that comes to rest on -0, and it gets there from +1, not from +0.) */
    TEST_ASSERT_EQUAL_HEX16(077776, m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_PIPAZ]);
}

static void test_a_velocity_increment_arriving_too_soon_is_lost(void)
{
    /* The AGC's whole idea of how fast it is going is the running total of
     * these pulses, so one lost here is wrong for ever. */
    TEST_ASSERT_TRUE(agc_imu_accelerate(m, AGC_IMU_Y, true));
    TEST_ASSERT_FALSE(agc_imu_accelerate(m, AGC_IMU_Y, true));
    TEST_ASSERT_EQUAL_UINT64(1, m->imu.pipa_pulses[AGC_IMU_Y]);
    TEST_ASSERT_EQUAL_UINT64(1, m->imu.pipa_pulses_refused);
}

/* --- coarse align ----------------------------------------------------------- */

static void test_coarse_align_moves_the_gimbal_and_the_cdu_follows(void)
{
    /* This is the one place the computer moves the platform rather than
     * measuring it: the drive pulses reach the gimbal, and the CDU reports the
     * movement back so the AGC's own angle counter tracks what it commanded. */
    set_ch12(AGC_CH12_COARSE_ALIGN);
    m->mem.erasable[CDUXD_ADDR] = 3;
    agc_channel_write(&m->channels, AGC_CH_GYRO, AGC_CH14_DRIVE_X);
    test_run_mcts(m, DRIVE_MCTS);

    TEST_ASSERT_EQUAL_INT32(3, m->imu.gimbal[AGC_IMU_X]);
    TEST_ASSERT_EQUAL_HEX16(3, m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_CDUX]);
}

static void test_without_coarse_align_the_platform_stays_put(void)
{
    /* Outside coarse align the gimbal is inertially fixed and the drive is
     * talking to the stabilisation loop, so the angle the computer reads does
     * not change just because the computer asked. */
    m->mem.erasable[CDUXD_ADDR] = 3;
    agc_channel_write(&m->channels, AGC_CH_GYRO, AGC_CH14_DRIVE_X);
    test_run_mcts(m, DRIVE_MCTS);

    TEST_ASSERT_EQUAL_INT32(0, m->imu.gimbal[AGC_IMU_X]);
    TEST_ASSERT_EQUAL_HEX16(0, m->mem.erasable[AGC_COUNTER_BASE + AGC_CNT_CDUX]);
    /* The pulses were still sent — they just went somewhere else. */
    TEST_ASSERT_EQUAL_INT32(3, m->cdu.driven[AGC_CDU_X]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_gyro_count_is_the_number_of_dincs_it_took);
    RUN_TEST(test_the_selection_bits_choose_the_axis);
    RUN_TEST(test_the_third_selection_bit_is_the_sign);
    RUN_TEST(test_selecting_no_gyro_torques_nothing);
    RUN_TEST(test_a_gyro_that_is_not_enabled_is_not_torqued);
    RUN_TEST(test_a_pipa_pulse_increments_its_counter);
    RUN_TEST(test_deceleration_counts_the_other_way);
    RUN_TEST(test_a_velocity_increment_arriving_too_soon_is_lost);
    RUN_TEST(test_coarse_align_moves_the_gimbal_and_the_cdu_follows);
    RUN_TEST(test_without_coarse_align_the_platform_stays_put);
    return UNITY_END();
}
