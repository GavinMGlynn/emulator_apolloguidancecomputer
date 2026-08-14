/* The Coupling Data Units: the AGC's window onto the gimbals and the optics.
 *
 * A CDU is an angle-to-digital converter with a digital-to-analogue half bolted
 * on. It watches one physical axis and, every time that axis turns through one
 * CDU count, sends the computer a pulse — PCDU going one way, MCDU the other —
 * which priority control services by incrementing or decrementing an erasable
 * counter. So the AGC never *reads* an angle. It keeps a running total that the
 * hardware nudges, and if it misses a pulse the total is wrong until something
 * zeroes it.
 *
 * Driving works the other way round and through different counters. The program
 * loads a *drive* counter (CDUXD and friends) with a number of pulses, enables
 * that axis in channel 14, and priority control runs DINC on it once per
 * scaler tick: each DINC steps the counter one toward zero and emits POUT or
 * MOUT — a plus or minus drive pulse to the CDU — until the count runs out and
 * ZOUT stops it. The rate is the scaler's, not the program's, so a large angle
 * simply takes longer.
 *
 * References: Information Series #30 paragraphs 30-90 through 30-99 (the CDU
 * Drive Control), table 30-5A (channel 12's zero and enable discretes) and
 * table 30-7 (which pulse train runs which sequence).
 *
 * What is *not* modelled here is the thing on the other end. There is no
 * gimbal, no optics head and no rendezvous radar, so an axis only moves when
 * something tells this module it has moved. The drive pulses are counted and
 * exposed rather than being fed back into an angle; closing that loop is the
 * IMU item in the completion plan, not this one.
 */
#ifndef AGC_CDU_H
#define AGC_CDU_H

#include <stdbool.h>
#include <stdint.h>

#include "../agc_word.h"

struct agc;

/* The five axes, in the order their counters sit in erasable memory. */
enum agc_cdu_axis {
    AGC_CDU_X,     /* inner gimbal  */
    AGC_CDU_Y,     /* middle gimbal */
    AGC_CDU_Z,     /* outer gimbal  */
    AGC_CDU_TRUN,  /* optics trunnion, or the rendezvous radar's equivalent */
    AGC_CDU_SHAFT, /* optics shaft */
    AGC_CDU_AXES
};

/* Channel 12's CDU discretes (Information Series #30, table 30-5A). The two
 * "zero" bits are held, not pulsed: while one is set the CDU counters stay at
 * zero and no pulse from that axis is accepted. */
#define AGC_CH12_ZERO_OPTICS_CDU AGC_BIT(1)  /* ZOPCDU; the LM's RR CDU */
#define AGC_CH12_ENABLE_OPTICS_EC AGC_BIT(2) /* ENEROP */
#define AGC_CH12_COARSE_ALIGN    AGC_BIT(4)  /* COARSE */
#define AGC_CH12_ZERO_IMU_CDU    AGC_BIT(5)  /* ZIMCDU */
#define AGC_CH12_ENABLE_IMU_EC   AGC_BIT(6)  /* ENERIM */

/* Channel 14's drive enables: bit 15 is the X axis and they descend from
 * there, which is why ZOUT clears `15 - axis` when a drive runs out. */
#define AGC_CH14_DRIVE_X AGC_BIT(15)
#define AGC_CH14_DRIVE_Y AGC_BIT(14)
#define AGC_CH14_DRIVE_Z AGC_BIT(13)
#define AGC_CH14_DRIVE_T AGC_BIT(12)
#define AGC_CH14_DRIVE_S AGC_BIT(11)

/* A CDU counter is a ones'-complement angle in units of the CDU's least count:
 * the full circle is 2^15 counts, so one count is about 39.6 arc seconds for
 * the gimbals. */
#define AGC_CDU_COUNTS_PER_REVOLUTION 32768

typedef struct agc_cdu {
    /* Drive pulses this axis has been sent since the machine started, and the
     * running total (plus minus minus). The physical CDU integrates these into
     * an angle; with nothing on the other end we keep the arithmetic so a test
     * or a frontend can see exactly what the program commanded. */
    int32_t driven[AGC_CDU_AXES];
    uint64_t drive_pulses[AGC_CDU_AXES];

    /* Pulses this module has fed *to* the machine, for the same reason. */
    uint64_t pulses_in[AGC_CDU_AXES];
    uint64_t pulses_refused[AGC_CDU_AXES];
} agc_cdu;

void agc_cdu_reset(agc_cdu *c);

/* Called once per timing pulse: applies the held zero discretes and the error
 * counter enables. */
void agc_cdu_tick(struct agc *m);

/* One CDU count of movement on an axis, as the converter reports it. Returns
 * false if the machine is not listening — the axis is being held at zero, or a
 * pulse is already waiting to be serviced. */
bool agc_cdu_pulse(struct agc *m, enum agc_cdu_axis axis, bool positive);

/* A drive pulse on its way out, called by POUT and MOUT during DINC. */
void agc_cdu_drive(struct agc *m, enum agc_cdu_axis axis, bool positive);

/* Which axis a counter address belongs to, or AGC_CDU_AXES for none. Both the
 * angle counters and the drive counters map onto the same five axes. */
enum agc_cdu_axis agc_cdu_axis_of_angle_counter(unsigned counter);
enum agc_cdu_axis agc_cdu_axis_of_drive_counter(unsigned counter);

/* Is this axis's error counter enabled? A disabled error counter is held at
 * zero, which is how the program stops a drive it has changed its mind about. */
bool agc_cdu_error_counter_enabled(const struct agc *m, enum agc_cdu_axis axis);

/* Is the CDU being held at zero? */
bool agc_cdu_zeroed(const struct agc *m, enum agc_cdu_axis axis);

#endif /* AGC_CDU_H */
