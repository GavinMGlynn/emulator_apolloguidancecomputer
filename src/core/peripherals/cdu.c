#include "cdu.h"

#include <string.h>

#include "../agc.h"

/* The angle counters sit at 032-036 and the drive counters at 040-044; the five
 * axes run in the same order through both. */
static const unsigned angle_counter[AGC_CDU_AXES] = {
    AGC_CNT_CDUX, AGC_CNT_CDUY, AGC_CNT_CDUZ, AGC_CNT_TRN, AGC_CNT_SHFT,
};
static const unsigned drive_counter[AGC_CDU_AXES] = {
    AGC_CNT_CDUXD, AGC_CNT_CDUYD, AGC_CNT_CDUZD, AGC_CNT_TRUND, AGC_CNT_SHAFTD,
};

/* The IMU's three gimbals answer to one pair of channel 12 discretes and the
 * optics to another, so the two halves of the CDU can be zeroed and enabled
 * independently — which is what lets the program realign the platform while
 * the optics keep tracking. */
static bool is_imu_axis(enum agc_cdu_axis axis)
{
    return axis <= AGC_CDU_Z;
}

void agc_cdu_reset(agc_cdu *c)
{
    memset(c, 0, sizeof *c);
}

enum agc_cdu_axis agc_cdu_axis_of_angle_counter(unsigned counter)
{
    for (unsigned a = 0; a < AGC_CDU_AXES; ++a) {
        if (angle_counter[a] == counter) {
            return (enum agc_cdu_axis)a;
        }
    }
    return AGC_CDU_AXES;
}

enum agc_cdu_axis agc_cdu_axis_of_drive_counter(unsigned counter)
{
    for (unsigned a = 0; a < AGC_CDU_AXES; ++a) {
        if (drive_counter[a] == counter) {
            return (enum agc_cdu_axis)a;
        }
    }
    return AGC_CDU_AXES;
}

bool agc_cdu_zeroed(const agc *m, enum agc_cdu_axis axis)
{
    agc_word ch12 = agc_channel_read(&m->channels, AGC_CH_IMU_CTL);
    agc_word bit = is_imu_axis(axis) ? AGC_CH12_ZERO_IMU_CDU : AGC_CH12_ZERO_OPTICS_CDU;
    return (ch12 & bit) != 0;
}

bool agc_cdu_error_counter_enabled(const agc *m, enum agc_cdu_axis axis)
{
    agc_word ch12 = agc_channel_read(&m->channels, AGC_CH_IMU_CTL);
    agc_word bit = is_imu_axis(axis) ? AGC_CH12_ENABLE_IMU_EC : AGC_CH12_ENABLE_OPTICS_EC;
    return (ch12 & bit) != 0;
}

bool agc_cdu_pulse(agc *m, enum agc_cdu_axis axis, bool positive)
{
    agc_cdu *c = &m->cdu;

    if (agc_cdu_zeroed(m, axis)) {
        /* The zero discrete does not clear the counter and then let it run; it
         * holds the whole converter at zero for as long as it is set, so the
         * pulse never reaches priority control at all. */
        c->pulses_refused[axis]++;
        return false;
    }

    unsigned counter = angle_counter[axis];
    if (m->cpu.counters[counter] != AGC_COUNT_NONE) {
        /* One request cell per counter: a pulse arriving before the last one
         * has been serviced is simply lost, and the angle is quietly wrong
         * until the next zero. This is a real failure mode of the machine, not
         * a limitation here, so it is counted rather than queued. */
        c->pulses_refused[axis]++;
        return false;
    }

    m->cpu.counters[counter] = positive ? AGC_COUNT_UP : AGC_COUNT_DOWN;
    c->pulses_in[axis]++;
    return true;
}

void agc_cdu_drive(agc *m, enum agc_cdu_axis axis, bool positive)
{
    agc_cdu *c = &m->cdu;
    c->driven[axis] += positive ? 1 : -1;
    c->drive_pulses[axis]++;
}

/* Deliberately does nothing to the counters in erasable memory, which is worth
 * saying out loud because the obvious reading of "zero IMU CDU's" is that the
 * discrete clears them.
 *
 * It does not. The discrete zeroes the *converter* — the thing outside the
 * computer — so that it stops tracking and stops sending pulses; the counters
 * in erasable are the program's own running totals and the program clears them
 * itself. Luminary 099 is explicit about it: `ZEROICDU  CAF ZERO ... TS CDUZ`,
 * commented "ZERO ICDU COUNTERS", called right after the discrete goes up.
 *
 * The same goes for the error-counter enables. They gate the error counter in
 * the IMU, not the drive counter in erasable, and an earlier version of this
 * file that held the latter at zero was caught within a minute by the counters
 * probe: it changed a measured counter storm from 529 MCTs to 482. */
void agc_cdu_tick(agc *m)
{
    (void)m;
}
