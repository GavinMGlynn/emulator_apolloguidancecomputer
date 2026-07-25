#include "scaler.h"

#include <string.h>

#include "../agc.h"

/* Scaler stage n as a mask. Stage 17 does not fit in an agc_word, so the scaler
 * keeps its own macro rather than using AGC_BIT. */
#define FS(n) (1u << ((n) - 1))

/* Falling edge of stage n (the "A" phase in the timer drawings). */
static bool edge_a(const agc_scaler *s, unsigned n)
{
    return ((s->state ^ s->prev) & FS(n)) != 0 && (s->state & FS(n)) == 0;
}

/* Rising edge of stage n (the "B" phase). */
static bool edge_b(const agc_scaler *s, unsigned n)
{
    return ((s->state ^ s->prev) & FS(n)) != 0 && (s->state & FS(n)) != 0;
}

void agc_scaler_reset(agc_scaler *s)
{
    memset(s, 0, sizeof *s);
}

void agc_scaler_update_interrupt_state(agc_scaler *s, bool iip)
{
    if (s->last_iip && !iip) {
        s->interrupt_ended = true;
    } else if (!s->last_iip && iip) {
        s->interrupt_started = true;
    }
    s->last_iip = iip;
}

static void raise_alarm(agc *m, agc_word bit)
{
    if (m->ignore_alarms || (m->alarm_inhibit & bit)) {
        return;
    }
    agc_cpu_write_channel(m, AGC_CH_ALARMS, bit);
    m->alarm_latched = true;
    agc_cpu_queue_gojam(&m->cpu);
}

void agc_scaler_tick(agc *m)
{
    agc_scaler *s = &m->scaler;
    agc_cpu *c = &m->cpu;

    s->prev = s->state;
    s->state++;

    /* F06B, 1.6 kHz: TIME6 counts down, but only while channel 13 bit 16 says
     * the descent-throttle interrupt is armed. */
    if (edge_b(s, 6)) {
        if (agc_cpu_read_channel(m, AGC_CH_MISC) & AGC_BIT(16)) {
            c->counters[AGC_CNT_TIME6] |= AGC_COUNT_DOWN;
        }
    }

    /* F09B, 200 Hz: TIME4, on the half of the cycle where stage 10 is low. */
    if (edge_b(s, 9) && (s->state & FS(10)) == 0) {
        c->counters[AGC_CNT_TIME4] |= AGC_COUNT_UP;
    }

    /* F10A, 100 Hz: TIME5, and the TC TRAP check. If the program has executed
     * nothing but TC/TCF since the last check it is stuck in a transfer-control
     * loop with interrupts off, and only a restart will free it. */
    if (edge_a(s, 10)) {
        c->counters[AGC_CNT_TIME5] |= AGC_COUNT_UP;
        if (!s->tc_started || !s->tc_ended) {
            raise_alarm(m, AGC_ALARM_TC_TRAP);
        }
    }

    /* F10B, 100 Hz: TIME1 and TIME3 together — TIME1/TIME2 is the machine's
     * clock, TIME3 the waitlist timer. Both edges of stage 10 do work, which is
     * why the pair is 100 Hz each but 200 Hz between them. */
    if (edge_b(s, 10)) {
        c->counters[AGC_CNT_TIME1] |= AGC_COUNT_UP;
        c->counters[AGC_CNT_TIME3] |= AGC_COUNT_UP;
        s->tc_started = false;
        s->tc_ended = false;
    }

    if (edge_b(s, 14)) {
        s->interrupt_started = false;
        s->interrupt_ended = false;
    }

    /* F17A, ~0.78 Hz: NIGHT WATCHMAN. The flight software must touch erasable
     * 067 at least this often; if it hasn't, it is not running its main loop. */
    if (edge_a(s, 17)) {
        if (!c->night_watchman) {
            raise_alarm(m, AGC_ALARM_NIGHT_WATCH);
        }
    }
    if (edge_b(s, 17)) {
        c->night_watchman = false;
    }

    /* The DSKY verb/noun flash: stages 16 and 17 both low. */
    s->flash_on = (s->state & (FS(16) | FS(17))) == 0;

    /* RUPT LOCK, checked at F14H (the rising edge of stage 12 while stage 13 is
     * set and stage 14 is not): either an interrupt has been in progress for
     * ~300 ms, or none has started in that time. Both mean the interrupt
     * machinery has jammed. */
    if (edge_b(s, 12) && (s->state & FS(13)) != 0 && (s->state & FS(14)) == 0) {
        if (!s->interrupt_started && !s->interrupt_ended) {
            raise_alarm(m, AGC_ALARM_RUPT_LOCK);
        }
    }
}
