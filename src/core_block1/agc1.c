/* The Block I machine: memory, the three-phase tick, and the decode.
 *
 * The structure mirrors `src/core/cpu/cpu.c` deliberately — same three phases,
 * same table-driven timepulse — so that the two cores can be read against each
 * other and the differences stand out rather than hiding in different shapes.
 */
#include "agc1.h"

#include <stdio.h>
#include <string.h>

/* --- memory ---------------------------------------------------------------- */

agc1_word agc1_read(agc1 *m, agc1_word address)
{
    if (address <= AGC1_ERASABLE_END) {
        /* 0-17 are the central and editing registers, not core: they read as
         * zero through this path and are reached by RSC instead. */
        if (address < 020u || address >= AGC1_ERASABLE_WORDS) {
            return 0;
        }
        agc1_word v = agc1_w(m->erasable[address] & ~(unsigned)AGC1_BIT(15));
        v = agc1_w(v | ((v & AGC1_BIT(16)) >> 1));
        m->erasable[address] = 0; /* the read is destructive */
        return v;
    }

    unsigned fixed;
    if (address <= AGC1_FIXED_FIXED_END || m->bank <= 3u) {
        fixed = address - AGC1_FIXED_FIXED_START;
    } else {
        fixed = (AGC1_FIXED_FIXED_END - AGC1_FIXED_FIXED_START + 1u)
                + (m->bank - 4u) * AGC1_FIXED_BANK_WORDS
                + (address - AGC1_FIXED_BANKED_START);
    }
    return fixed < AGC1_FIXED_WORDS ? m->fixed[fixed] : 0;
}

void agc1_write(agc1 *m, agc1_word address, agc1_word data)
{
    if (address >= 020u && address < AGC1_ERASABLE_WORDS) {
        m->erasable[address] = data;
    }
    /* Fixed memory is rope: writes to it go nowhere, as they do in the article. */
}

long agc1_load_rope(agc1 *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    long count = 0;
    unsigned char pair[2];
    unsigned addr = 0;
    while (addr < AGC1_FIXED_WORDS && fread(pair, 1, 2, f) == 2) {
        m->fixed[addr++] = agc1_w(((unsigned)pair[0] << 8) | pair[1]);
        count++;
    }
    bool bad = ferror(f) != 0;
    fclose(f);
    return bad ? -1 : count;
}

/* --- start-up -------------------------------------------------------------- */

static const agc1_subinst *find(uint8_t stage, bool extend, agc1_word sq)
{
    for (size_t i = 0; i < agc1_subinst_count; ++i) {
        const agc1_subinst *si = &agc1_subinst_table[i];
        if (si->extend == extend && si->stage == stage
            && (si->sq_value & si->sq_mask) == (sq & si->sq_mask)) {
            return si;
        }
    }
    return NULL;
}

void agc1_start(agc1 *m)
{
    /* GOJAM, Block I style: stage 2 at 2030 with bank 1, and every output
     * register cleared but OUT4 (ND-1021041 p. 4-231, via the reference
     * model). */
    m->gojam_pending = false;
    m->inhibit_interrupts = false;
    m->inkl = false;
    m->iip = false;
    m->overflow = false;
    m->z = 02030u;
    m->st = 2;
    m->st_next = 0;
    m->bank = 1;
    m->extend = false;
    m->extend_next = false;
    m->fetch_next_instruction = false;
    m->timepulse = 1;
    for (unsigned i = 0; i < 4; ++i) {
        m->out[i] = 0;
    }
    m->subinst = &agc1_subinst_table[0]; /* STD2 */
    agc1_update_adder(m);
}

void agc1_init(agc1 *m)
{
    memset(m, 0, sizeof *m);
    agc1_start(m);
}

/* --- the three phases ------------------------------------------------------ */

static void before_timepulse(agc1 *m)
{
    if (m->timepulse == 1) {
        if (m->gojam_pending) {
            agc1_start(m);
        }
        if (m->inkl) {
            for (unsigned c = 0; c < AGC1_COUNTER_COUNT; ++c) {
                if (m->counters[c] == AGC1_COUNT_UP) {
                    m->subinst = &agc1_subinst_pinc;
                    break;
                }
                if (m->counters[c] == AGC1_COUNT_DOWN) {
                    m->subinst = &agc1_subinst_minc;
                    break;
                }
            }
        } else if (m->fetch_next_instruction) {
            m->fetch_next_instruction = false;
            m->extend_next = false;
            m->overflow = false;
        }
    }

    /* Data arrives from memory before T6 here, not T4 as on Block II. */
    if (m->timepulse == 6 && m->s >= 020u) {
        /* Multiply and divide inhibit the fetch during their middle stage. */
        const bool inhibited = m->extend && m->st == 1
                               && (m->sq == 011u || m->sq == 012u);
        if (!inhibited) {
            m->g = agc1_read(m, m->s);
        }
    }
}

static void run_timepulse(agc1 *m)
{
    const agc1_subinst *si = m->subinst;
    if (!si) {
        return;
    }
    for (uint8_t r = 0; r < si->row_count; ++r) {
        const agc1_pulse_row *row = &si->rows[r];
        if (row->tp != m->timepulse || (m->br & row->br_mask) != row->br_value) {
            continue;
        }
        for (unsigned i = 0; i < AGC1_MAX_PULSES_PER_TIMEPULSE; ++i) {
            if (row->pulse[i] == AGC1_P_NONE) {
                break;
            }
            agc1_pulse_apply(m, (enum agc1_pulse)row->pulse[i]);
        }
        break;
    }
}

static void after_timepulse(agc1 *m)
{
    /* The rewrite half of the destructive erasable cycle. */
    if (m->timepulse == 10 && m->s >= 020u && m->s <= AGC1_ERASABLE_END) {
        agc1_write(m, m->s, m->g);
    }

    if (m->timepulse == 12) {
        if (!m->inkl) {
            m->st = m->st_next;
            m->st_next = 0;
        }

        m->inkl = false;
        if (!m->ignore_counters) {
            for (unsigned c = 0; c < AGC1_COUNTER_COUNT; ++c) {
                if (m->counters[c] != AGC1_COUNT_NONE) {
                    m->inkl = true;
                    break;
                }
            }
        }

        if (m->fetch_next_instruction) {
            bool rupt = false;
            for (unsigned i = 0; i < AGC1_RUPT_COUNT; ++i) {
                if (m->interrupts[i]) {
                    rupt = true;
                    break;
                }
            }
            /* Block I carries EXTEND as negative overflow in B rather than in a
             * flip-flop of its own — the order code space is that tight. */
            if (((m->b >> 14) & 3u) == 0x2u) { /* 10: negative overflow */
                m->extend_next = true;
            }
            if (rupt && !m->ignore_interrupts && !m->inhibit_interrupts
                && !m->iip && !m->overflow) {
                const agc1_subinst *r1 = find(1, false, 003u);
                if (r1) {
                    m->sq = r1->sq_value;
                    m->extend = r1->extend;
                    m->st = r1->stage;
                }
            } else {
                m->sq = agc1_w((m->b & 0170000u) >> 12); /* B13-16 into SQ1-4 */
                m->extend = m->extend_next;
            }
        }

        const agc1_subinst *next = find(m->st, m->extend, m->sq);
        m->subinst = next ? next : &agc1_subinst_table[0];
    }

    m->timepulse = m->timepulse < 12 ? (uint8_t)(m->timepulse + 1) : 1;
}

void agc1_tick(agc1 *m)
{
    m->write_bus = 0;
    before_timepulse(m);
    run_timepulse(m);
    after_timepulse(m);
    m->timepulses++;
}

void agc1_tick_mct(agc1 *m)
{
    for (unsigned i = 0; i < AGC1_TIMEPULSES_PER_MCT; ++i) {
        agc1_tick(m);
    }
}

int agc1_format_state(const agc1 *m, char *buf, size_t len)
{
    return snprintf(buf, len,
                    "%-6s T%-2u A=%06o Q=%06o Z=%06o LP=%06o G=%06o B=%06o "
                    "S=%04o SQ=%02o ST=%u BR=%02o BANK=%02o U=%06o",
                    m->subinst ? m->subinst->name : "?", m->timepulse,
                    m->a, m->q, m->z, m->lp, m->g, m->b,
                    m->s, m->sq, m->st, m->br, m->bank, m->u);
}
