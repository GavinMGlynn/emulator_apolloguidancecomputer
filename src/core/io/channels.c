#include "channels.h"

#include <string.h>

void agc_channels_reset(agc_channels *c)
{
    memset(c, 0, sizeof *c);

    /* Channels 30-33 carry inverted discretes: a line that is not being pulled
     * low reads as 1. Reset them to all-ones, then assert the two bits a
     * healthy, powered machine holds low on channel 30: TEMP IN LIMITS (bit 8)
     * and IMU OPERATE (bit 9). Without those the flight software declares an
     * ISS failure within a second of GOJAM. */
    for (unsigned n = 030u; n <= 033u; ++n) {
        agc_channel_write(c, n, AGC_WORD_MASK);
    }
    agc_channel_write(c, 030u,
                      agc_w(AGC_WORD_MASK & ~(unsigned)(AGC_BIT(8) | AGC_BIT(9))));
}

agc_word agc_channel_read(const agc_channels *c, unsigned n)
{
    return n < AGC_CHANNEL_COUNT ? c->ch[n].value : 0;
}

void agc_channel_write(agc_channels *c, unsigned n, agc_word v)
{
    if (n >= AGC_CHANNEL_COUNT) {
        return;
    }
    agc_channel *ch = &c->ch[n];
    ch->prev = ch->value;
    ch->value = v;
    ch->diff = agc_w(ch->value ^ ch->prev);
}

bool agc_channel_changed(const agc_channels *c, unsigned n, agc_word mask)
{
    return n < AGC_CHANNEL_COUNT && (c->ch[n].diff & mask) == mask;
}

bool agc_channel_set(const agc_channels *c, unsigned n, agc_word mask)
{
    return n < AGC_CHANNEL_COUNT && (c->ch[n].value & mask) == mask;
}
