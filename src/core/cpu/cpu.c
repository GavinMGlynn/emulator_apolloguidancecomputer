#include "cpu.h"

#include <string.h>

#include "../agc.h"
#include "pulses.h"

/* Erasable 067 is the night watchman's cell: the flight software is required to
 * touch it regularly, and the hardware restarts the computer if it doesn't. */
#define NIGHT_WATCHMAN_ADDR 067u

void agc_cpu_update_adder(agc_cpu *cpu)
{
    unsigned sum = (unsigned)cpu->x + (unsigned)cpu->y;
    unsigned carry = cpu->explicit_carry ? 1u : 0u;
    if (!cpu->no_eac && !cpu->mp3a) {
        /* Ones' complement: a carry out of bit 16 comes back in at bit 1. This
         * is why the AGC has two zeroes and why NEACON exists — multiply needs
         * the carry suppressed across the partial-product steps.
         *
         * Two signals suppress it, and the memo names only one. The service
         * gates form the carry into bit 1 as CINORM = NOR(NEAC, EAC, MP3A)
         * (module A7 gate 33457): besides the NEACON..NEACOF latch, the bare
         * MP3 decode line inhibits the carry for the whole of that
         * subinstruction. It has to, because NEACOF fires at MP3 T6 while the
         * multiply's final sum is not formed until the RU WA at T11 — with
         * only the latch, that sum end-around-carries and -0 + 1 comes out as
         * +1 instead of +0. Measured: 8 of 100 operand pairs wrong.
         * FINDINGS #11. */
        carry |= (sum >> 16) & 1u;
    }
    cpu->u = agc_w(sum + carry);
}

void agc_cpu_update_bb(agc_cpu *cpu)
{
    cpu->bb = agc_w(cpu->fb | (unsigned)(cpu->eb >> 8));
}

void agc_cpu_update_eb_fb(agc_cpu *cpu)
{
    cpu->eb = agc_w((unsigned)(cpu->bb & 7u) << 8);
    cpu->fb = agc_w(cpu->bb & AGC_BITS(11, 15));
}

agc_word agc_cpu_read_channel(agc *m, unsigned n)
{
    switch (n) {
    case AGC_CH_L:    return m->cpu.l;
    case AGC_CH_Q:    return m->cpu.q;
    case AGC_CH_FEXT: return m->cpu.fext;
    default: {
        /* Channels are 15 bits; bit 16 mirrors bit 15 on the way out so the
         * adder and the sign tests see a well-formed word. */
        unsigned v = agc_channel_read(&m->channels, n) & ~(unsigned)AGC_BIT(16);
        v |= (v & AGC_BIT(15)) << 1;
        return agc_w(v);
    }
    }
}

void agc_cpu_write_channel(agc *m, unsigned n, agc_word v)
{
    switch (n) {
    case AGC_CH_L: m->cpu.l = v; return;
    case AGC_CH_Q: m->cpu.q = v; return;
    case AGC_CH_FEXT:
        m->cpu.fext = agc_w(v & 0160u);
        break; /* and fall through to the ordinary channel store */
    default:
        break;
    }

    unsigned t = v & ~(unsigned)AGC_BIT(15);
    t |= (t & AGC_BIT(16)) >> 1;
    t &= ~(unsigned)AGC_BIT(16);
    agc_channel_write(&m->channels, n, agc_w(t));
}

void agc_cpu_queue_gojam(agc_cpu *cpu)
{
    cpu->gojam_pending = true;
}

/* GOJAM: the hardware restart. Not a reset — erasable memory survives, which is
 * the whole point of the restart-protection scheme the flight software builds
 * on top of it. */
static void gojam(agc *m)
{
    agc_cpu *c = &m->cpu;

    /* GOJ1 is stage 1 of the TC sequence with SQ forced to zero; it lands the
     * program at 04000. */
    for (size_t i = 0; i < agc_subinst_count; ++i) {
        if (agc_subinst_table[i].stage == 1 && !agc_subinst_table[i].extend
            && agc_subinst_table[i].sq_mask == 070 && agc_subinst_table[i].sq_value == 0) {
            c->subinst = &agc_subinst_table[i];
            break;
        }
    }

    c->sq = 0;
    c->extend = false;
    c->extend_next = false;
    c->st = 1;
    c->st_next = 0;
    c->restart = true;
    c->inkl = false;
    c->inhibit_interrupts = false;
    c->pseudo = false;
    c->no_eac = false;
    c->mp3a = false;
    c->iip = false;
    c->gojam_pending = false;

    /* Output channels are cleared so no thruster, gimbal drive or lamp is left
     * commanded across the restart. */
    static const unsigned cleared[] = { 005, 006, 010, 011, 012, 013, 014, 034, 035 };
    for (size_t i = 0; i < sizeof cleared / sizeof *cleared; ++i) {
        agc_channel_write(&m->channels, cleared[i], 0);
    }
    agc_channel_write(&m->channels, 033,
                      agc_w(agc_channel_read(&m->channels, 033) & ~(unsigned)AGC_BIT(11)));

    memset(c->interrupts, 0, sizeof c->interrupts);
}

void agc_cpu_start(agc *m)
{
    agc_cpu *c = &m->cpu;
    memset(c, 0, sizeof *c);
    c->timepulse = 1;

    agc_cpu_update_eb_fb(c);
    agc_cpu_update_adder(c);
    agc_channels_reset(&m->channels);
    gojam(m);
}

/* Pick the involuntary sequence for the highest-priority pending counter. */
static const agc_subinst *counter_sequence(const agc_cpu *c)
{
    for (unsigned i = 0; i < AGC_COUNTER_COUNT; ++i) {
        unsigned dir = c->counters[i];
        if (dir == AGC_COUNT_NONE) {
            continue;
        }
        switch (agc_counter_kind(i)) {
        case AGC_CK_PINC:
            return &agc_subinst_pinc;
        case AGC_CK_PINC_MINC:
            return dir == AGC_COUNT_UP ? &agc_subinst_pinc : &agc_subinst_minc;
        case AGC_CK_DINC:
            return &agc_subinst_dinc;
        case AGC_CK_PCDU_MCDU:
            return dir == AGC_COUNT_UP ? &agc_subinst_pcdu : &agc_subinst_mcdu;
        case AGC_CK_SHINC:
        case AGC_CK_SHINC_SHANC:
            /* PROVISIONAL: the serial-input sequences need the uplink and radar
             * shift registers, which do not exist yet. Leaving the request
             * pending would deadlock priority control, so drop it and let the
             * program see no data. See docs/COMPLETION_PLAN.md. */
            return NULL;
        }
    }
    return NULL;
}

/* Everything the memory and priority-control logic do between timing pulses. */
static void before_timepulse(agc *m)
{
    agc_cpu *c = &m->cpu;

    c->mcro = false;

    if (c->timepulse == 1) {
        if (c->gojam_pending) {
            gojam(m);
        }
        if (c->inkl) {
            const agc_subinst *seq = counter_sequence(c);
            if (seq) {
                c->subinst = seq;
            } else {
                c->inkl = false;
            }
        }
        if (c->st != 2) {
            c->fetch_next_instruction = false;
            c->extend_next = false;
        }
    }

    /* PIFL is a one-quarter-MCT latch. */
    if (c->timepulse == 2 || c->timepulse == 5 || c->timepulse == 8 || c->timepulse == 11) {
        c->pifl = false;
    }

    /* The memory read completes before T5, so a T5 RG sees the fetched word. */
    if (c->timepulse == 5) {
        if (c->s <= 01777u) {
            /* Addresses 0-7 are the central registers, reached by RSC, not by a
             * memory cycle; and an MCT that talks to a channel runs no erasable
             * cycle at all even though S holds a low address. */
            if (c->s >= 010u && !c->channel_access) {
                c->s_writeback = c->s;
                agc_word addr = agc_erasable_absolute(c->s, c->eb);
                c->g = agc_memory_read_erasable(&m->mem, addr);
                if (addr == NIGHT_WATCHMAN_ADDR) {
                    c->night_watchman = true;
                }
            }
        } else if (!c->dv || c->st < 3) {
            /* Fixed memory. During divide stages 3, 7, 6 and 4 the sense
             * amplifiers are inhibited, so no fetch happens. */
            unsigned addr = agc_fixed_absolute(c->s, c->fb, c->fext);
            if (!agc_memory_fixed_parity_ok(&m->mem, addr) && !m->ignore_alarms
                && !(m->alarm_inhibit & AGC_ALARM_PARITY_FAIL)) {
                agc_cpu_write_channel(m, AGC_CH_ALARMS, AGC_ALARM_PARITY_FAIL);
                m->alarm_latched = true;
                agc_cpu_queue_gojam(c);
            }
            c->g = agc_memory_read_fixed(&m->mem, addr);
        }
    }

    /* The rewrite half of the destructive erasable cycle, before T10. */
    if (c->timepulse == 10 && c->s_writeback != 0) {
        agc_word addr = agc_erasable_absolute(c->s_writeback, c->eb);
        agc_memory_write_erasable(&m->mem, addr, c->g);
        c->s_writeback = 0;
        if (addr == NIGHT_WATCHMAN_ADDR) {
            c->night_watchman = true;
        }
    }
}

/* Assert this subinstruction's control pulses for this timing pulse. */
static void run_timepulse(agc *m)
{
    const agc_cpu *c = &m->cpu;
    const agc_subinst *si = c->subinst;
    if (!si) {
        return;
    }
    for (uint8_t r = 0; r < si->row_count; ++r) {
        const agc_pulse_row *row = &si->rows[r];
        if (row->tp != c->timepulse) {
            continue;
        }
        if ((c->br & row->br_mask) != row->br_value) {
            continue;
        }
        for (unsigned i = 0; i < AGC_MAX_PULSES_PER_TIMEPULSE; ++i) {
            if (row->pulse[i] == AGC_P_NONE) {
                break;
            }
            agc_pulse_apply(m, (enum agc_pulse)row->pulse[i]);
        }
        /* At most one row of a sequence can match a given (tp, br). */
        break;
    }
}

/* Decode SQ, ST and EXTEND into the next subinstruction. */
static const agc_subinst *decode(const agc_cpu *c)
{
    for (size_t i = 0; i < agc_subinst_count; ++i) {
        const agc_subinst *si = &agc_subinst_table[i];
        if (si->extend == c->extend && si->stage == c->st
            && (c->sq & si->sq_mask) == si->sq_value) {
            return si;
        }
    }
    return NULL;
}

static void after_timepulse(agc *m)
{
    agc_cpu *c = &m->cpu;

    /* The write lines exist only within a timing pulse. */
    c->write_bus = 0;

    /* An MCT ends at T12 — except during divide, where DVST licenses the
     * sequence to restage at T3. */
    bool end_of_mct = (!c->dv && c->timepulse == 12) || (c->dv && c->timepulse == 3);

    if (end_of_mct) {
        c->st = c->st_next;
        c->st_next = 0;
        c->inkl = false;

        if (c->fetch_next_instruction) {
            c->channel_access = false;

            /* Counters outrank interrupts, which outrank the program. */
            if (!c->pseudo && !m->ignore_counters) {
                for (unsigned i = 0; i < AGC_COUNTER_COUNT; ++i) {
                    if (c->counters[i] != AGC_COUNT_NONE) {
                        c->inkl = true;
                        break;
                    }
                }
            }

            bool rupt_pending = false;
            if (!c->inkl) {
                for (unsigned i = 0; i < AGC_RUPT_COUNT; ++i) {
                    if (c->interrupts[i]) {
                        rupt_pending = true;
                        break;
                    }
                }
            }

            /* An interrupt may not break into an overflowed accumulator: A's
             * two sign bits would be truncated on the way to erasable memory
             * and the overflow lost. */
            unsigned signs = (unsigned)(c->a & AGC_BITS(15, 16)) >> 14;
            bool a_overflow = (signs == 0x1u || signs == 0x2u);

            if (rupt_pending && !m->ignore_interrupts && !c->inhibit_interrupts
                && !c->iip && !c->extend_next && !c->pseudo && !a_overflow) {
                c->sq = 0007u; /* RUPT0 */
                c->extend = true;
            } else {
                c->sq = agc_w((unsigned)(c->b & AGC_BITS(10, 14)) >> 9);
                c->sq = agc_w(c->sq | ((unsigned)(c->b & AGC_BIT(16)) >> 10));
                c->extend = c->extend_next;
            }
        }

        const agc_subinst *next = decode(c);
        if (!next) {
            /* Every (SQ, ST, EXTEND) triple the hardware can produce is in the
             * table, so this is a bug in us, not a machine state. Fall back to
             * STD2 (which just fetches the next instruction) rather than
             * stopping, and leave the state visible for the caller to notice. */
            next = &agc_subinst_table[0];
            c->s = agc_w(c->z & AGC_ADDR_MASK);
        }
        c->subinst = next;
        /* MP3A is a decode line off SQ and ST, not a control pulse, so it
         * follows what was *decoded* — a counter sequence that steals the MCT
         * ahead of MP3 leaves SQ and the stage counter alone and so runs with
         * the carry still inhibited. Deliberately not cleared where priority
         * control injects an involuntary sequence. */
        c->mp3a = strcmp(next->name, "MP3") == 0;
    }

    c->timepulse = c->timepulse < 12 ? (uint8_t)(c->timepulse + 1) : 1;
}

void agc_cpu_tick(agc *m)
{
    before_timepulse(m);
    run_timepulse(m);
    after_timepulse(m);
}
