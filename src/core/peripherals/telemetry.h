/* Downlink, outlink and radar: the three remaining serial paths.
 *
 * All three are the same trick in different directions, and none of them is
 * DMA — the AGC has none. Words are assembled or taken apart one bit at a time
 * by counter sequences stolen from the program.
 *
 *   - **Downlink** is the odd one out: it does not shift at all. The Downlink
 *     Converter reads channels 34 and 35 whole and serialises them itself, and
 *     all the AGC sees is DOWNRUPT arriving to say "the next word, please".
 *   - **Outlink** is a shift *out*: the program loads counter OUTLNK (0057) and
 *     the Outlink Control asks for SHINC once per bit, transmitting whatever
 *     falls off the top. No interrupt — the transmitter is pulling, not the
 *     computer pushing.
 *   - **Radar** is a shift *in*, exactly like the uplink: a flag bit and
 *     fifteen data bits into counter RNRAD (0046), and RADARRUPT when the flag
 *     is shifted out.
 *
 * References: Information Series #30 paragraph 30-70 (which control converts
 * what, and which counter each uses), table 30-5B for channel 13's radar mode
 * selection and downlink order bit, and table 30-7 for the request lines.
 *
 * Nothing here runs on its own. There is no telemetry ground station and no
 * radar, so a frontend has to say "the transmitter wants a word" or "the radar
 * is answering"; left alone, these paths are silent, which is what an AGC on a
 * bench with nothing plugged into it does.
 */
#ifndef AGC_TELEMETRY_H
#define AGC_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include "../agc_word.h"

struct agc;

/* Channel 13's radar control (Information Series #30, table 30-5B). The three
 * selection bits are numbered the other way round from channel 14's gyro
 * selection — a is bit 3 here — which is worth stating because getting it
 * backwards selects a plausible wrong radar. */
#define AGC_CH13_RADAR_SEL_C AGC_BIT(1)
#define AGC_CH13_RADAR_SEL_B AGC_BIT(2)
#define AGC_CH13_RADAR_SEL_A AGC_BIT(3)
#define AGC_CH13_RADAR_STROBE AGC_BIT(4) /* transmit a control signal to radar */
#define AGC_CH13_DOWNLINK_ORDER AGC_BIT(7)

/* Channel 14 bit 1 asks the Outlink Control for one word. */
#define AGC_CH14_OUTLINK_WORD AGC_BIT(1)

/* What the radar is being asked for. */
enum agc_radar_mode {
    AGC_RADAR_NONE,
    AGC_RADAR_RR_RANGE,      /* RRRANG */
    AGC_RADAR_RR_RANGE_RATE, /* RRRARA */
    AGC_RADAR_LR_X_VELOCITY, /* LRXVEL */
    AGC_RADAR_LR_Y_VELOCITY, /* LRYVEL */
    AGC_RADAR_LR_Z_VELOCITY, /* LRZVEL */
    AGC_RADAR_LR_RANGE,      /* LRRANG */
};

/* The downlink word rate. The Apollo telemetry format ran at 50 words per
 * second on the low bit rate and 100 on the high one; which one is in use is
 * the ground's business, not the computer's, so it is set here rather than
 * derived from anything the AGC does. */
#define AGC_DOWNLINK_LOW_RATE_HZ  50u
#define AGC_DOWNLINK_HIGH_RATE_HZ 100u

typedef struct agc_telemetry {
    /* Downlink: how many words the converter has taken, and the last one, so a
     * frontend can watch a downlist go by without a socket. */
    uint64_t downlink_words;
    agc_word downlink_last_34;
    agc_word downlink_last_35;
    /* Zero means no telemetry equipment is attached and DOWNRUPT never fires,
     * which is the state a machine on a bench is in. */
    uint32_t downlink_rate_hz;
    uint32_t downlink_countdown;

    /* Outlink: bits shifted out, and the last sixteen of them, most recent in
     * bit 0. */
    uint64_t outlink_bits;
    uint32_t outlink_shift_register;

    /* Radar. */
    uint64_t radar_bits_accepted;
    uint64_t radar_bits_refused;
    uint64_t radar_words_sent;
    uint32_t radar_pending;
    uint8_t radar_pending_bits;
} agc_telemetry;

void agc_telemetry_reset(agc_telemetry *t);

/* Called once per timing pulse: runs the downlink word clock and feeds any
 * queued radar bits. */
void agc_telemetry_tick(struct agc *m);

/* Set the downlink word rate; 0 detaches the telemetry equipment. */
void agc_downlink_set_rate(struct agc *m, uint32_t words_per_second);

/* The converter has taken a word: capture channels 34 and 35 and ask for the
 * next one. A frontend with its own telemetry clock can call this directly. */
void agc_downlink_take_word(struct agc *m);

/* One bit leaving counter OUTLNK, called by SHINC when it is shifting that
 * counter. */
void agc_outlink_bit(struct agc *m, bool one);

/* Which radar the program has selected, from channel 13 bits 1-3. */
enum agc_radar_mode agc_radar_selected(const struct agc *m);

/* Queue a radar answer: a flag bit and fifteen data bits into RNRAD, raising
 * RADARRUPT when the flag shifts out. */
void agc_radar_send(struct agc *m, agc_word data);

/* True while a radar word is still arriving. */
bool agc_radar_busy(const agc_telemetry *t);

#endif /* AGC_TELEMETRY_H */
