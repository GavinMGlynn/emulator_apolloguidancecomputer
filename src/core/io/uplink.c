#include "uplink.h"

#include <string.h>

#include "../agc.h"

void agc_uplink_reset(agc_uplink *u)
{
    memset(u, 0, sizeof *u);
    u->since_last = AGC_UPLINK_BIT_PULSES; /* ready for the first bit */
}

bool agc_uplink_busy(const agc_uplink *u)
{
    return u->pending_bits != 0;
}

agc_word agc_uplink_keycode(unsigned code)
{
    /* Bits 1-5 the code, 6-10 the code again, 11-15 its complement. The flight
     * software takes all three apart and requires them to agree, so a word
     * built any other way is rejected by the machine rather than by us. */
    unsigned c = code & 037u;
    return agc_w(c | (c << 5) | ((~c & 037u) << 10));
}

/* Is the Inlink Control listening at all? Channel 13 bit 6 is the program's
 * block; the cabin switch is the crew's. Bit 5 selects crosslink over uplink,
 * and since nothing here models a crosslink partner, a program that switches to
 * it stops hearing us — which is the honest behaviour. */
static bool listening(const agc *m)
{
    if (m->uplink.blocked_by_switch) {
        return false;
    }
    agc_word ch13 = agc_channel_read(&m->channels, AGC_CH_MISC);
    return (ch13 & (AGC_CH13_BLOCK_INLINK | AGC_CH13_CROSSLINK)) == 0;
}

bool agc_uplink_bit(agc *m, bool one)
{
    agc_uplink *u = &m->uplink;

    if (!listening(m)) {
        u->bits_refused++;
        return false;
    }

    if (u->since_last < AGC_UPLINK_BIT_PULSES) {
        /* Two bits inside one 156 microsecond window. The hardware drops the
         * second and says so on channel 33, which is an inverted channel — the
         * bit reads as ZERO when the condition is present. */
        agc_word ch33 = agc_channel_read(&m->channels, 033u);
        agc_channel_write(&m->channels, 033u,
                          agc_w(ch33 & ~(unsigned)AGC_CH33_UPLINK_TOO_FAST));
        u->bits_refused++;
        return false;
    }

    /* Priority control has one request cell per counter and no room for a bit
     * value, so the direction carries it: a one is SHANC, a zero is SHINC
     * (Information Series #30 table 30-7). */
    m->cpu.counters[AGC_CNT_INLINK] = one ? AGC_COUNT_UP : AGC_COUNT_DOWN;
    u->since_last = 0;
    u->bits_accepted++;
    return true;
}

void agc_uplink_send(agc *m, agc_word data)
{
    agc_uplink *u = &m->uplink;
    /* The flag bit rides above the fifteen data bits and goes out first. */
    u->pending = (1u << 15) | (data & 077777u);
    u->pending_bits = AGC_UPLINK_WORD_BITS;
    u->words_sent++;
}

void agc_uplink_tick(agc *m)
{
    agc_uplink *u = &m->uplink;

    if (u->since_last < AGC_UPLINK_BIT_PULSES) {
        u->since_last++;
    }
    if (u->pending_bits == 0 || u->since_last < AGC_UPLINK_BIT_PULSES) {
        return;
    }

    bool bit = (u->pending & (1u << (u->pending_bits - 1u))) != 0;
    if (agc_uplink_bit(m, bit)) {
        u->pending_bits--;
    }
}
