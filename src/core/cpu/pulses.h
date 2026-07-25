/* Control-pulse execution.
 *
 * Every pulse is one small, total action on machine state. Read pulses OR onto
 * the write lines (they are a wired-OR bus, which is why two read pulses in one
 * timing pulse combine rather than conflict — MSK0 T9 does exactly that);
 * write pulses latch the write lines into a register; test pulses set the
 * branch registers.
 *
 * Definitions are in AGC4 Memo #9, transcribed in
 * docs/references/AgcPulsesAndSequences.txt.
 */
#ifndef AGC_PULSES_H
#define AGC_PULSES_H

#include "subinst.h"

struct agc;

void agc_pulse_apply(struct agc *m, enum agc_pulse p);

#endif /* AGC_PULSES_H */
