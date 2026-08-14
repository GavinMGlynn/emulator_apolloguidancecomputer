#include "telemetry.h"

#include <string.h>

#include "../agc.h"

/* One radar bit per 156.25 microseconds, the same rate limit the Inlink
 * Control works to; the two shift-in paths share their timing. */
#define RADAR_BIT_PULSES 160u

void agc_telemetry_reset(agc_telemetry *t)
{
    memset(t, 0, sizeof *t);
}

void agc_downlink_set_rate(agc *m, uint32_t words_per_second)
{
    m->telemetry.downlink_rate_hz = words_per_second;
    m->telemetry.downlink_countdown =
        words_per_second ? AGC_TIMEPULSE_CLOCK_HZ / words_per_second : 0;
}

void agc_downlink_take_word(agc *m)
{
    agc_telemetry *t = &m->telemetry;
    t->downlink_last_34 = agc_channel_read(&m->channels, 034u);
    t->downlink_last_35 = agc_channel_read(&m->channels, 035u);
    t->downlink_words++;
    /* The converter has what it came for; DOWNRUPT is it asking for the next
     * word, which is why the interrupt comes *after* the read and not before. */
    m->cpu.interrupts[AGC_RUPT_DOWNRUPT] = true;
}

void agc_outlink_bit(agc *m, bool one)
{
    agc_telemetry *t = &m->telemetry;
    t->outlink_shift_register = ((t->outlink_shift_register << 1) | (one ? 1u : 0u));
    t->outlink_bits++;
}

enum agc_radar_mode agc_radar_selected(const agc *m)
{
    agc_word ch13 = agc_channel_read(&m->channels, AGC_CH_MISC);
    const bool a = (ch13 & AGC_CH13_RADAR_SEL_A) != 0;
    const bool b = (ch13 & AGC_CH13_RADAR_SEL_B) != 0;
    const bool c = (ch13 & AGC_CH13_RADAR_SEL_C) != 0;

    /* Table 30-5B's own order, a b c. Two of the eight combinations are
     * deliberately "none" — 000 and 011 — so a program can select nothing
     * without clearing the whole channel. */
    if (!a && !b && c)  { return AGC_RADAR_RR_RANGE; }
    if (!a && b && !c)  { return AGC_RADAR_RR_RANGE_RATE; }
    if (a && !b && !c)  { return AGC_RADAR_LR_X_VELOCITY; }
    if (a && !b && c)   { return AGC_RADAR_LR_Y_VELOCITY; }
    if (a && b && !c)   { return AGC_RADAR_LR_Z_VELOCITY; }
    if (a && b && c)    { return AGC_RADAR_LR_RANGE; }
    return AGC_RADAR_NONE;
}

void agc_radar_send(agc *m, agc_word data)
{
    agc_telemetry *t = &m->telemetry;
    t->radar_pending = (1u << 15) | (data & 077777u);
    t->radar_pending_bits = 16u;
    t->radar_words_sent++;
}

bool agc_radar_busy(const agc_telemetry *t)
{
    return t->radar_pending_bits != 0;
}

static void radar_tick(agc *m)
{
    agc_telemetry *t = &m->telemetry;
    static const unsigned counter = AGC_CNT_RNRAD;

    if (t->radar_pending_bits == 0) {
        return;
    }
    if (m->timepulses % RADAR_BIT_PULSES != 0) {
        return;
    }
    if (m->cpu.counters[counter] != AGC_COUNT_NONE) {
        t->radar_bits_refused++;
        return;
    }

    bool bit = (t->radar_pending & (1u << (t->radar_pending_bits - 1u))) != 0;
    m->cpu.counters[counter] = bit ? AGC_COUNT_UP : AGC_COUNT_DOWN;
    t->radar_pending_bits--;
    t->radar_bits_accepted++;
}

void agc_telemetry_tick(agc *m)
{
    agc_telemetry *t = &m->telemetry;

    if (t->downlink_rate_hz != 0) {
        if (t->downlink_countdown > 0) {
            t->downlink_countdown--;
        }
        if (t->downlink_countdown == 0) {
            agc_downlink_take_word(m);
            t->downlink_countdown = AGC_TIMEPULSE_CLOCK_HZ / t->downlink_rate_hz;
        }
    }

    radar_tick(m);
}
