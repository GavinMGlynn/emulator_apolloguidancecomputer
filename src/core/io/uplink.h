/* The Inlink Control: serial data from the ground, one bit at a time.
 *
 * The AGC has no receiver. What it has is a counter — INLINK, erasable 045 —
 * and two request lines into priority control. Every arriving bit steals one
 * MCT to run SHINC (shift a zero in) or SHANC (shift a one in), so an uplinked
 * word is assembled a bit at a time by the sequence generator itself, in
 * cycles taken from whatever the program was doing.
 *
 * Each word is sixteen bits: a flag bit, always a one, then fifteen data bits.
 * The flag walks up the register as the data arrives behind it, and when it
 * reaches the top it is shifted out — into bit 16 of the adder, where TSGN
 * catches it — and that is what raises UPRUPT. So the interrupt is not counted
 * or timed: it falls out of the shifting, which is why a word with a lost bit
 * simply never interrupts.
 *
 * References: Information Series #30 paragraphs 30-117 through 30-119A (the
 * Inlink Control, its channel 13 gating and its 156 microsecond rate limit) and
 * table 30-7 (which request line runs which sequence). The bit-to-sequence
 * mapping was confirmed against the gate netlist, module A19.
 */
#ifndef AGC_UPLINK_H
#define AGC_UPLINK_H

#include <stdbool.h>
#include <stdint.h>

#include "../agc_word.h"

struct agc;

/* Channel 13's two Inlink Control bits (Information Series #30, table 30-5B).
 * Bit 6 blocks the counter requests outright; with it clear, bit 5 chooses
 * which pair of input lines is listened to. */
#define AGC_CH13_CROSSLINK    AGC_BIT(5)
#define AGC_CH13_BLOCK_INLINK AGC_BIT(6)

/* Channel 33 bit 11: two bits arrived closer together than the hardware can
 * service. Channel 33 is one of the inverted channels, so the bit reads as a
 * ZERO when the condition is present. */
#define AGC_CH33_UPLINK_TOO_FAST AGC_BIT(11)

/* One bit per 156.25 microseconds — the period of scaler stage 4, which is what
 * sets the enable flip-flop the Inlink Control gates its outputs with. At the
 * timing-pulse rate that is exactly 160 pulses, so no rounding is involved. */
#define AGC_UPLINK_BIT_PULSES 160u

/* A word is a flag bit and fifteen data bits. */
#define AGC_UPLINK_WORD_BITS 16u

typedef struct agc_uplink {
    /* Bits still to be delivered, most significant first, and how many. */
    uint32_t pending;
    uint8_t pending_bits;

    /* Timing pulses since the last bit was accepted, for the rate limit. */
    uint32_t since_last;

    /* The uplink switch in the cabin. Set means the crew has blocked the
     * uplink, and no request reaches priority control however fast the ground
     * sends. Distinct from channel 13 bit 6, which is the program's own block. */
    bool blocked_by_switch;

    /* Counted so a frontend can tell "the ground sent nothing" from "the ground
     * sent and the machine refused". */
    uint64_t bits_accepted;
    uint64_t bits_refused;
    uint64_t words_sent;
} agc_uplink;

void agc_uplink_reset(agc_uplink *u);

/* Called once per timing pulse: releases the next queued bit when the rate
 * limit allows. */
void agc_uplink_tick(struct agc *m);

/* Deliver one bit now, as the receiving equipment does. Returns false if the
 * machine refused it — blocked, or arriving inside the 156 microsecond window,
 * in which case channel 33 bit 11 is set to say so. */
bool agc_uplink_bit(struct agc *m, bool one);

/* Queue a whole word: the flag bit, then `data`'s fifteen bits most
 * significant first. The bits go out at the hardware's own rate, one every
 * 156 microseconds, so the word takes about 2.5 ms to arrive — during which
 * the program keeps running and loses sixteen MCTs to the shifting. */
void agc_uplink_send(struct agc *m, agc_word data);

/* True while a queued word is still going out. */
bool agc_uplink_busy(const agc_uplink *u);

/* Pack a five-bit key code the way the ground does: the code, the code again,
 * and its complement, which is the redundancy the flight software checks (see
 * UPRUPT/UPTEST in PINBALL). */
agc_word agc_uplink_keycode(unsigned code);

#endif /* AGC_UPLINK_H */
