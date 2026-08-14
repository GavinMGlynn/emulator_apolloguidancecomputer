#include "imu.h"

#include <string.h>

#include "../agc.h"
#include "cdu.h"

static const unsigned pipa_counter[AGC_IMU_AXES] = {
    AGC_CNT_PIPAX, AGC_CNT_PIPAY, AGC_CNT_PIPAZ,
};

void agc_imu_reset(agc_imu *imu)
{
    memset(imu, 0, sizeof *imu);
}

enum agc_imu_axis agc_imu_selected_gyro(const agc *m, bool *negative)
{
    agc_word ch14 = agc_channel_read(&m->channels, AGC_CH_GYRO);
    if (negative) {
        *negative = (ch14 & AGC_CH14_GYRO_SEL_C) != 0;
    }

    /* Table 30-5C's own truth table, in its own order: a and b together pick
     * the axis and 00 picks nothing, which is why an enabled but unselected
     * gyro drive quietly does nothing at all. */
    const bool a = (ch14 & AGC_CH14_GYRO_SEL_A) != 0;
    const bool b = (ch14 & AGC_CH14_GYRO_SEL_B) != 0;
    if (!a && b) {
        return AGC_IMU_X;
    }
    if (a && !b) {
        return AGC_IMU_Y;
    }
    if (a && b) {
        return AGC_IMU_Z;
    }
    return AGC_IMU_AXES;
}

bool agc_imu_gyro_pulse(agc *m)
{
    agc_imu *imu = &m->imu;
    agc_word ch14 = agc_channel_read(&m->channels, AGC_CH_GYRO);

    if ((ch14 & AGC_CH14_GYRO_ENABLE) == 0 || (ch14 & AGC_CH14_GYRO_DRIVE) == 0) {
        imu->gyro_pulses_refused++;
        return false;
    }

    bool negative = false;
    enum agc_imu_axis axis = agc_imu_selected_gyro(m, &negative);
    if (axis == AGC_IMU_AXES) {
        imu->gyro_pulses_refused++;
        return false;
    }

    imu->gyro_torque[axis] += negative ? -1 : 1;
    imu->gyro_pulses[axis]++;
    return true;
}

void agc_imu_drive_gimbal(agc *m, enum agc_imu_axis axis, bool positive)
{
    /* Outside coarse align the platform is inertially fixed: the drive pulses
     * are going to the stabilisation loop, not to the gimbal, and the angle the
     * computer reads does not change because the computer asked it to. Under
     * coarse align it does, and the CDU reports the movement straight back so
     * the AGC's own counter follows what it commanded. */
    if (!agc_channel_set(&m->channels, AGC_CH_IMU_CTL, AGC_CH12_COARSE_ALIGN)) {
        return;
    }

    m->imu.gimbal[axis] += positive ? 1 : -1;
    agc_cdu_pulse(m, (enum agc_cdu_axis)axis, positive);
}

bool agc_imu_accelerate(agc *m, enum agc_imu_axis axis, bool positive)
{
    agc_imu *imu = &m->imu;
    unsigned counter = pipa_counter[axis];

    if (m->cpu.counters[counter] != AGC_COUNT_NONE) {
        /* One request cell, no queue: a velocity increment arriving before the
         * last has been serviced is simply lost, and the AGC's idea of how fast
         * it is going is permanently that much wrong. */
        imu->pipa_pulses_refused++;
        return false;
    }

    m->cpu.counters[counter] = positive ? AGC_COUNT_UP : AGC_COUNT_DOWN;
    imu->pipa_pulses[axis]++;
    return true;
}
