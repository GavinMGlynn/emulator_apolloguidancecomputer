/* The Inertial Measurement Unit: three gimbals, three gyros, three
 * accelerometers.
 *
 * The AGC's relationship with the IMU runs entirely through counters and
 * discretes, and in three separate directions:
 *
 *   - **Angles come in** through the CDUs (see cdu.h), which is why nothing in
 *     this file reports an angle directly.
 *   - **Torque goes out** to the gyros. The program loads GYROD with a pulse
 *     count, picks an axis and a sign in channel 14, and priority control walks
 *     the counter down at 3.2 kHz; each DINC is one torque pulse. That is how
 *     the platform is *fine* aligned — nudged a fraction of an arc second at a
 *     time until the stars line up.
 *   - **Velocity comes in** from the PIPAs, one pulse per increment of velocity
 *     along each axis, straight into three PINC/MINC counters. The AGC's whole
 *     notion of where it is and how fast is the running total of those pulses.
 *
 * Coarse align is the fourth path and it is the CDU's: with channel 12 bit 4
 * set the computer drives the gimbals through the CDU error counters, and the
 * CDU reports the movement back so its own angle counters follow. That loop is
 * closed here because closing it needs something that *has* an angle.
 *
 * Reference: Information Series #30 table 30-5C for channel 14 — bit 6 enables
 * the gyro, bits 7, 8 and 9 select axis and sign, bit 10 runs the drive — and
 * table 30-7 for the counters.
 *
 * What is deliberately absent is dynamics. Nothing here integrates an
 * acceleration, models gimbal rates or drifts a gyro: a frontend says the
 * vehicle moved and this turns that into the pulses the hardware would send.
 * The AGC cannot tell the difference, because pulses are all it ever sees.
 */
#ifndef AGC_IMU_H
#define AGC_IMU_H

#include <stdbool.h>
#include <stdint.h>

#include "../agc_word.h"

struct agc;

enum agc_imu_axis { AGC_IMU_X, AGC_IMU_Y, AGC_IMU_Z, AGC_IMU_AXES };

/* Channel 14's gyro control (Information Series #30, table 30-5C). The
 * selection is three bits: a and b choose the axis, c is the sign. */
#define AGC_CH14_GYRO_ENABLE AGC_BIT(6)
#define AGC_CH14_GYRO_SEL_B  AGC_BIT(7)
#define AGC_CH14_GYRO_SEL_A  AGC_BIT(8)
#define AGC_CH14_GYRO_SEL_C  AGC_BIT(9) /* minus sign */
#define AGC_CH14_GYRO_DRIVE  AGC_BIT(10)

typedef struct agc_imu {
    /* Where the gimbals are, in CDU counts. Only coarse align moves them from
     * in here; otherwise the platform is inertially fixed and a frontend moves
     * the *vehicle* around it. */
    int32_t gimbal[AGC_IMU_AXES];

    /* Torque delivered to each gyro, signed, and the raw pulse count. Fine
     * alignment is measured in these. */
    int32_t gyro_torque[AGC_IMU_AXES];
    uint64_t gyro_pulses[AGC_IMU_AXES];
    uint64_t gyro_pulses_refused;

    /* Velocity increments handed to the PIPAs. */
    uint64_t pipa_pulses[AGC_IMU_AXES];
    uint64_t pipa_pulses_refused;
} agc_imu;

void agc_imu_reset(agc_imu *imu);

/* A gyro torque pulse, called by POUT and MOUT when DINC is walking GYROD
 * down. Which gyro and which way come from channel 14, not from the counter —
 * the pulse train carries only magnitude. Returns false if the program has not
 * enabled and selected a gyro, in which case the pulse goes nowhere, as it
 * does in the hardware. */
bool agc_imu_gyro_pulse(struct agc *m);

/* A CDU drive pulse reaching a gimbal. Only does anything under coarse align:
 * that is what coarse align *is*, the computer moving the platform rather than
 * measuring it. */
void agc_imu_drive_gimbal(struct agc *m, enum agc_imu_axis axis, bool positive);

/* One PIPA pulse: the vehicle gained one velocity increment along an axis.
 * Returns false if the counter has not yet been serviced, in which case the
 * increment is lost — which is a real way for the AGC's velocity to go wrong. */
bool agc_imu_accelerate(struct agc *m, enum agc_imu_axis axis, bool positive);

/* Which gyro channel 14 currently selects, or AGC_IMU_AXES for none. */
enum agc_imu_axis agc_imu_selected_gyro(const struct agc *m, bool *negative);

#endif /* AGC_IMU_H */
