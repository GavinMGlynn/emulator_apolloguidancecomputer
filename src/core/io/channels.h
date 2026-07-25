/* I/O channels.
 *
 * The AGC reaches the outside world through 16-bit channels addressed by the
 * low six bits of S.  Channels 1 and 2 are not channels at all: they alias the
 * L and Q central registers, and the CPU intercepts them before they reach
 * here.  Channel 7 is FEXT (the superbank select).
 *
 * Channels 30-33 are *inverted* — an open input line reads as 1 — so they reset
 * to all-ones rather than zero.  Several discretes are edge-sensitive, so every
 * channel remembers its previous value and the bits that changed on the last
 * write; that is how channel 12's CDU-zero and channel 14's gyro-enable
 * discretes are detected without polling.
 */
#ifndef AGC_CHANNELS_H
#define AGC_CHANNELS_H

#include <stdbool.h>

#include "../agc_word.h"

/* Six address bits, so 64 real channels. */
#define AGC_CHANNEL_COUNT 0100u

/* Channels the core itself reacts to. */
#define AGC_CH_L        001u /* aliases the L register */
#define AGC_CH_Q        002u /* aliases the Q register */
#define AGC_CH_FEXT     007u /* superbank select */
#define AGC_CH_OUT1     005u
#define AGC_CH_OUT2     006u
#define AGC_CH_DSKY     010u /* DSKY display, written a digit-group at a time */
#define AGC_CH_DSKY_IN  015u /* main DSKY keyboard */
#define AGC_CH_DSKY_IN2 016u /* navigation-panel keyboard / optics marks */
#define AGC_CH_LAMPS    011u
#define AGC_CH_IMU_CTL  012u
#define AGC_CH_MISC     013u
#define AGC_CH_GYRO     014u
#define AGC_CH_ALARMS   077u /* which hardware alarm caused the last restart */

typedef struct agc_channel {
    agc_word value;
    agc_word prev;
    agc_word diff;
} agc_channel;

typedef struct agc_channels {
    agc_channel ch[AGC_CHANNEL_COUNT];
} agc_channels;

void agc_channels_reset(agc_channels *c);

/* Raw accessors: no L/Q/FEXT aliasing, no bit-15/16 fixups. The CPU's
 * agc_cpu_read_channel / agc_cpu_write_channel wrap these. */
agc_word agc_channel_read(const agc_channels *c, unsigned n);
void     agc_channel_write(agc_channels *c, unsigned n, agc_word v);

/* True if every bit in `mask` changed on the most recent write to channel n. */
bool agc_channel_changed(const agc_channels *c, unsigned n, agc_word mask);
/* True if every bit in `mask` is currently set in channel n. */
bool agc_channel_set(const agc_channels *c, unsigned n, agc_word mask);

#endif /* AGC_CHANNELS_H */
