/* The scaler: the AGC's clock tree below the timing-pulse rate.
 *
 * Reference: Information Series #5 ("Timer") and the SCALER module sheets in
 * docs/references/block2-schematics/.
 *
 * The 2.048 MHz master oscillator is divided by two to give the 1.024 MHz
 * timing-pulse clock; twelve timing pulses make one MCT of 11.71875 us. The
 * scaler is a 17-stage binary counter clocked at 102.4 kHz — the timing-pulse
 * rate divided by ten — and everything periodic in the machine hangs off one of
 * its stages:
 *
 *   F01A  51.2 kHz  CDU pulse train
 *   F05A   3.2 kHz  CDU/gyro drive commands from channel 14
 *   F06B   1.6 kHz  TIME6 decrement (only while channel 13 bit 16 is set)
 *   F09B    200 Hz  TIME4 increment, DSKY keyboard scan
 *   F10A    100 Hz  TIME5 increment, TC TRAP check
 *   F10B    100 Hz  TIME1 and TIME3 increment
 *   F17A/B  0.78 Hz NIGHT WATCHMAN check
 *
 * "A" is the falling edge of that stage, "B" the rising edge. The alarms live
 * here because they are all "did the expected thing happen before this edge?"
 * questions: TC TRAP (the program has executed nothing but TC/TCF for 5-10 ms),
 * RUPT LOCK (an interrupt has been in progress, or absent, for ~300 ms), and
 * NIGHT WATCHMAN (the program has not touched erasable 067 for ~1.28 s). Each
 * raises its bit in channel 77 and forces a GOJAM.
 */
#ifndef AGC_SCALER_H
#define AGC_SCALER_H

#include <stdbool.h>
#include <stdint.h>

#include "../agc_word.h"

struct agc;

/* Timing pulses per scaler tick. */
#define AGC_SCALER_DIVISOR 10u

/* Channel 77 alarm bits. */
#define AGC_ALARM_PARITY_FAIL  AGC_BIT(1)
#define AGC_ALARM_TC_TRAP      AGC_BIT(3)
#define AGC_ALARM_RUPT_LOCK    AGC_BIT(4)
#define AGC_ALARM_NIGHT_WATCH  AGC_BIT(5)

typedef struct agc_scaler {
    uint32_t state;
    uint32_t prev;

    /* TC TRAP: set when a TC/TCF starts, and when a non-TC MCT reaches T4. */
    bool tc_started, tc_ended;
    /* RUPT LOCK: the same question asked of interrupt entry and exit. */
    bool interrupt_started, interrupt_ended, last_iip;

    bool flash_on; /* DSKY verb/noun flash phase, ~1.28 s square wave */
} agc_scaler;

void agc_scaler_reset(agc_scaler *s);

/* Advance one scaler stage-0 tick (every AGC_SCALER_DIVISOR timing pulses). */
void agc_scaler_tick(struct agc *m);

/* Fed from the CPU each timing pulse so the RUPT LOCK edge detector can see
 * interrupt entry and exit. */
void agc_scaler_update_interrupt_state(agc_scaler *s, bool iip);

#endif /* AGC_SCALER_H */
