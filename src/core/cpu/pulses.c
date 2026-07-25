#include "pulses.h"

#include "../agc.h"

/* Read pulses ----------------------------------------------------------------
 * Each ORs a value onto the write lines. */

static void p_ra(agc *m) { m->cpu.write_bus |= m->cpu.a; }
static void p_rb(agc *m) { m->cpu.write_bus |= m->cpu.b; }
static void p_rg(agc *m) { m->cpu.write_bus |= m->cpu.g; }
static void p_rq(agc *m) { m->cpu.write_bus |= m->cpu.q; }
static void p_ru(agc *m) { m->cpu.write_bus |= m->cpu.u; }
static void p_rz(agc *m) { m->cpu.write_bus |= m->cpu.z; }

static void p_rc(agc *m) { m->cpu.write_bus |= agc_w(m->cpu.b ^ AGC_WORD_MASK); }

static void p_r15(agc *m) { m->cpu.write_bus |= 000015u; }
static void p_r6(agc *m) { m->cpu.write_bus |= 000006u; }
static void p_r1c(agc *m) { m->cpu.write_bus |= 0177776u; }
static void p_rb1(agc *m) { m->cpu.write_bus |= 1u; }
static void p_rb2(agc *m) { m->cpu.write_bus |= 2u; }
static void p_rstrt(agc *m) { m->cpu.write_bus |= 004000u; }

/* RB1 conditional on the outcome of TSGU. */
static void p_rb1f(agc *m)
{
    if (m->cpu.br & 0x2u) {
        m->cpu.write_bus |= 1u;
    }
}

/* L reads onto the write lines with its sign duplicated: L1-14 to WL1-14 and
 * L16 to both WL15 and WL16. */
static void p_rl(agc *m)
{
    unsigned v = m->cpu.l & AGC_BITS(1, 14);
    v |= (unsigned)(m->cpu.l & AGC_BIT(16)) >> 1;
    v |= m->cpu.l & AGC_BIT(16);
    m->cpu.write_bus |= agc_w(v);
}

/* The uncorrected sum: U1-14, then U15 into both WL15 and WL16. Used by the
 * ones'-complement counter sequences and by MSU, where the overflow-corrected
 * RU would give the wrong answer. */
static void p_rus(agc *m)
{
    unsigned v = m->cpu.u & AGC_BITS(1, 14);
    v |= m->cpu.u & AGC_BIT(15);
    v |= (unsigned)(m->cpu.u & AGC_BIT(15)) << 1;
    m->cpu.write_bus |= agc_w(v);
}

/* The low ten bits of B: the operand address of the instruction being executed.
 * Only ten because addresses 0-1777 are erasable; fixed addressing goes through
 * the bank registers. */
static void p_rl10bb(agc *m) { m->cpu.write_bus |= m->cpu.b & AGC_BITS(1, 10); }

/* The central store selected by S (0-7). */
static void p_rsc(agc *m)
{
    agc_cpu *c = &m->cpu;
    switch (c->s) {
    case 0: p_ra(m); break;
    case 1: p_rl(m); break;
    case 2: p_rq(m); break;
    case 3: c->write_bus |= c->eb; break;
    case 4:
        /* FB's bit 15 is duplicated into bit 16 so that a copy landing in
         * erasable memory survives the 15-bit round trip. */
        c->write_bus |= c->fb;
        c->write_bus |= agc_w((unsigned)(c->fb & AGC_BIT(15)) << 1);
        break;
    case 5: p_rz(m); break;
    case 6:
        c->write_bus |= c->bb;
        c->write_bus |= agc_w((unsigned)(c->bb & AGC_BIT(15)) << 1);
        break;
    default: /* 7 is the hard-wired zero register: nothing on the lines. */
        break;
    }
}

/* Read the both-bank configuration back out: EB into WL1-3, FB into WL11-16. */
static void p_rbbk(agc *m)
{
    m->cpu.write_bus |= agc_w((unsigned)(m->cpu.eb >> 8) & AGC_BITS(1, 3));
    m->cpu.write_bus |= agc_w(m->cpu.fb & AGC_BITS(11, 15));
    m->cpu.write_bus |= agc_w((unsigned)(m->cpu.fb & AGC_BIT(15)) << 1);
}

static void p_rch(agc *m)
{
    agc_cpu *c = &m->cpu;
    if (c->s == AGC_CH_L) {
        p_rl(m);
    } else if (c->s == AGC_CH_Q) {
        p_rq(m);
    } else {
        c->write_bus |= agc_cpu_read_channel(m, c->s & 077u);
    }
    /* An MCT that touches a channel does not run an erasable cycle, even though
     * S looks like an erasable address. */
    c->channel_access = true;
}

/* The address of the highest-priority interrupt requested; all zeroes if none.
 * Vectors are at 04000 + 4n. */
static void p_rrpa(agc *m)
{
    for (unsigned r = 0; r < AGC_RUPT_COUNT; ++r) {
        if (m->cpu.interrupts[r]) {
            m->cpu.write_bus |= agc_w(004000u + r * 4u);
            return;
        }
    }
}

/* The address of the highest-priority counter request, and clear that request.
 * Reading it *is* the acknowledgement; there is no separate clear. */
static void p_rsct(agc *m)
{
    for (unsigned i = 0; i < AGC_COUNTER_COUNT; ++i) {
        if (m->cpu.counters[i] != AGC_COUNT_NONE) {
            m->cpu.write_bus |= agc_w(i + AGC_COUNTER_BASE);
            m->cpu.counters[i] = AGC_COUNT_NONE;
            return;
        }
    }
}

/* Write pulses ---------------------------------------------------------------- */

static void p_wa(agc *m) { m->cpu.a = m->cpu.write_bus; }
static void p_wb(agc *m) { m->cpu.b = m->cpu.write_bus; }
static void p_wl(agc *m) { m->cpu.l = m->cpu.write_bus; }
static void p_wq(agc *m) { m->cpu.q = m->cpu.write_bus; }
static void p_wz(agc *m) { m->cpu.z = m->cpu.write_bus; }
static void p_ws(agc *m) { m->cpu.s = agc_w(m->cpu.write_bus & AGC_ADDR_MASK); }

/* WG writes the memory buffer, except for the four editing registers at
 * 0020-0023, which shift or cycle on the way in. That is the whole of the AGC's
 * shift capability: there is no shift instruction, only these addresses. */
static void p_wg(agc *m)
{
    agc_cpu *c = &m->cpu;
    unsigned wl = c->write_bus;
    unsigned v = wl;

    /* If an erasable cycle is in flight, the editing decision follows the
     * address that cycle started with, not whatever S holds now. */
    agc_word addr = c->s_writeback ? c->s_writeback : c->s;

    switch (addr) {
    case 020u: /* CYR — cycle right */
        v = ((wl & ~(unsigned)AGC_BITS(15, 16)) >> 1) | ((wl & 1u) << 15);
        v |= (wl & AGC_BIT(16)) >> 2;
        break;
    case 021u: /* SR — shift right, sign-preserving */
        v = ((wl & ~(unsigned)AGC_BITS(15, 16)) >> 1) | (wl & AGC_BIT(16));
        v |= (wl & AGC_BIT(16)) >> 2;
        break;
    case 022u: /* CYL — cycle left */
        v = ((wl & ~(unsigned)AGC_BITS(14, 15)) << 1)
            | ((wl & AGC_BIT(16)) >> 15)
            | ((wl & AGC_BIT(14)) << 2);
        break;
    case 023u: /* EDOP — seven bits right, for interpreter opcodes */
        v = (wl & AGC_BITS(8, 14)) >> 7;
        break;
    default:
        break;
    }
    c->g = agc_w(v);
}

/* Write the central store selected by S (0-7). */
static void p_wsc(agc *m)
{
    agc_cpu *c = &m->cpu;
    unsigned wl = c->write_bus;
    switch (c->s) {
    case 0: c->a = c->write_bus; break;
    case 1: c->l = c->write_bus; break;
    case 2: c->q = c->write_bus; break;
    case 3:
        /* EB carries the erasable bank in bits 9-11, already aligned to the
         * 256-word stride so it ORs straight onto an in-bank offset. */
        c->eb = agc_w(wl & AGC_BITS(9, 11));
        agc_cpu_update_bb(c);
        break;
    case 4:
        /* Bit 15 of a word that has been through erasable memory is the
         * duplicated sign, not data; take the real bit 15 from bit 16. */
        c->fb = agc_w((wl & AGC_BITS(11, 15)) & ~(unsigned)AGC_BIT(15));
        c->fb = agc_w(c->fb | ((wl & AGC_BIT(16)) >> 1));
        agc_cpu_update_bb(c);
        break;
    case 5: c->z = c->write_bus; break;
    case 6:
        c->bb = agc_w(wl & ~(unsigned)AGC_BIT(15));
        c->bb = agc_w(c->bb | ((wl & AGC_BIT(16)) >> 1));
        agc_cpu_update_eb_fb(c);
        break;
    default:
        break;
    }
}

static void p_wch(agc *m)
{
    agc_cpu *c = &m->cpu;
    unsigned n = c->s & 077u;
    if (n == AGC_CH_L) {
        p_wl(m);
    } else if (n == AGC_CH_Q) {
        p_wq(m);
    } else {
        unsigned v = c->write_bus & ~(unsigned)AGC_BIT(15);
        v |= (c->write_bus & AGC_BIT(16)) >> 1;
        agc_cpu_write_channel(m, n, agc_w(v));
    }
    c->channel_access = true;
}

/* Adder input pulses ---------------------------------------------------------- */

static void p_wy(agc *m)
{
    m->cpu.explicit_carry = false;
    m->cpu.x = 0;
    m->cpu.y = m->cpu.write_bus;
    agc_cpu_update_adder(&m->cpu);
}

static void p_wy12(agc *m)
{
    m->cpu.explicit_carry = false;
    m->cpu.x = 0;
    m->cpu.y = agc_w(m->cpu.write_bus & AGC_ADDR_MASK);
    agc_cpu_update_adder(&m->cpu);
}

/* WYD loads Y shifted left one place — the AGC's multiply and divide shift.
 * WL16 goes into Y1 as well (a rotate) unless the end-around carry is inhibited
 * by NEACON, a shift-in sequence is running, or PIFL has blocked it. */
static void p_wyd(agc *m)
{
    agc_cpu *c = &m->cpu;
    c->explicit_carry = false;
    c->x = 0;
    unsigned v = (unsigned)(c->write_bus & AGC_BITS(1, 14)) << 1;
    v |= c->write_bus & AGC_BIT(16);
    if (!c->no_eac && !c->shinc && !c->pifl) {
        v |= (unsigned)(c->write_bus & AGC_BIT(16)) >> 15;
    }
    c->y = agc_w(v);
    agc_cpu_update_adder(c);
}

static void p_a2x(agc *m) { m->cpu.x = m->cpu.a; agc_cpu_update_adder(&m->cpu); }
static void p_ci(agc *m) { m->cpu.explicit_carry = true; agc_cpu_update_adder(&m->cpu); }
static void p_ponex(agc *m) { m->cpu.x |= 1u; agc_cpu_update_adder(&m->cpu); }
static void p_ptwox(agc *m) { m->cpu.x |= 2u; agc_cpu_update_adder(&m->cpu); }
static void p_monex(agc *m) { m->cpu.x |= 0177776u; agc_cpu_update_adder(&m->cpu); }
static void p_b15x(agc *m) { m->cpu.x |= AGC_BIT(15); agc_cpu_update_adder(&m->cpu); }

/* Clear X if TSGU found the sum non-negative. Divide's conditional subtract. */
static void p_clxc(agc *m)
{
    if ((m->cpu.br & 0x2u) == 0) {
        m->cpu.x = 0;
        agc_cpu_update_adder(&m->cpu);
    }
}

static void p_neacon(agc *m) { m->cpu.no_eac = true; agc_cpu_update_adder(&m->cpu); }
static void p_neacof(agc *m) { m->cpu.no_eac = false; agc_cpu_update_adder(&m->cpu); }

/* Test pulses ------------------------------------------------------------------
 * BR is held as (BR1 << 1) | BR2, matching the memo's "01"/"10" notation. */

static void p_tsgn(agc *m)
{
    m->cpu.br = (uint8_t)((m->cpu.br & 0x1u) | ((m->cpu.write_bus & AGC_BIT(16)) ? 0x2u : 0u));
}

static void p_tsgn2(agc *m)
{
    m->cpu.br = (uint8_t)((m->cpu.br & 0x2u) | ((m->cpu.write_bus & AGC_BIT(16)) ? 0x1u : 0u));
}

static void p_tsgu(agc *m)
{
    m->cpu.br = (uint8_t)((m->cpu.br & 0x1u) | ((m->cpu.u & AGC_BIT(16)) ? 0x2u : 0u));
}

/* Test the write lines for -0 (all ones). The reason ones' complement needs a
 * dedicated pulse: -0 and +0 are numerically equal but not bit-equal, and CCS
 * must tell them apart. */
static void p_tmz(agc *m)
{
    if (m->cpu.write_bus == 0177777u) {
        m->cpu.br |= 0x1u;
    } else {
        m->cpu.br = (uint8_t)(m->cpu.br & 0x2u);
    }
}

/* Test G for +0. Unlike TMZ this only ever sets BR2; a non-zero G leaves the
 * previous result standing, which is how CCS combines TMZ and TPZG. */
static void p_tpzg(agc *m)
{
    if (m->cpu.g == 0) {
        m->cpu.br |= 0x1u;
    }
}

static void p_tov(agc *m)
{
    switch ((unsigned)(m->cpu.write_bus & AGC_BITS(15, 16)) >> 14) {
    case 0x1u: m->cpu.br = 0x1u; break; /* positive overflow */
    case 0x2u: m->cpu.br = 0x2u; break; /* negative overflow */
    default:   m->cpu.br = 0x0u; break;
    }
}

static void p_tl15(agc *m)
{
    m->cpu.br = (uint8_t)((m->cpu.br & 0x1u) | ((m->cpu.l & AGC_BIT(15)) ? 0x2u : 0u));
}

static void p_st1(agc *m) { m->cpu.st_next |= 1u; }
static void p_st2(agc *m) { m->cpu.st_next |= 2u; }

/* INDEX 0017 is RESUME, not an index: divert to stage 3 (RSM3). */
static void p_trsm(agc *m)
{
    if (m->cpu.s == 017u) {
        p_st2(m);
    }
}

/* Sequencing and flag pulses ----------------------------------------------------- */

static void p_nisq(agc *m) { m->cpu.fetch_next_instruction = true; }
static void p_ext(agc *m) { m->cpu.extend_next = true; }

/* RAD reads the next instruction — normally RG, but the three pseudo-codes are
 * not instructions at all: they are TC to addresses 3, 4 and 6, and the
 * hardware recognises the *address* and turns the fetch into a no-op plus a
 * flag change. That is why RELINT/INHINT/EXTEND take a full MCT and why they
 * cannot be indexed. */
static void p_rad(agc *m)
{
    agc_cpu *c = &m->cpu;
    switch (c->g) {
    case 3: /* RELINT */
        c->inhibit_interrupts = false;
        c->pseudo = true;
        p_rz(m);
        p_st2(m);
        break;
    case 4: /* INHINT */
        c->inhibit_interrupts = true;
        c->pseudo = true;
        p_rz(m);
        p_st2(m);
        break;
    case 6: /* EXTEND */
        c->extend_next = true;
        c->pseudo = true;
        p_rz(m);
        p_st2(m);
        break;
    default:
        c->pseudo = false;
        p_rg(m);
        break;
    }
}

static void p_krpt(agc *m)
{
    unsigned idx = (unsigned)(m->cpu.s - 04000u) / 4u;
    if (idx < AGC_RUPT_COUNT) {
        m->cpu.interrupts[idx] = false;
    }
    m->cpu.iip = true;
}

static void p_z15(agc *m) { m->cpu.z |= AGC_BIT(15); }
static void p_z16(agc *m) { m->cpu.z |= AGC_BIT(16); }
static void p_l16(agc *m) { m->cpu.l |= AGC_BIT(16); }

static void p_pifl(agc *m) { m->cpu.pifl = (m->cpu.l & AGC_BIT(15)) != 0; }

/* Divide staging. DVST advances a grey-coded stage counter and permits the MCT
 * to end at T3 instead of T12; STAGE commits the advance; RSTSTG ends divide. */
static void p_dvst(agc *m) { m->cpu.dv = true; m->cpu.dv_stage++; }
static void p_stage(agc *m) { m->cpu.st_next = (uint8_t)(((7u << m->cpu.dv_stage) >> 3) & 7u); }
static void p_rststg(agc *m) { m->cpu.dv = false; m->cpu.dv_stage = 0; }

/* Implicit hardware signals, not in the memo's pulse list. */
static void p_1xp10(agc *m) { m->cpu.g = 0; }
static void p_8xp5(agc *m) { m->cpu.s |= 04000u; }

/* RESUME dropping the interrupt-in-progress line. Interrupts are not nested on
 * the AGC: with IIP set, no further interrupt is taken, so a RESUME that failed
 * to clear it would lock the machine out of its own interrupt system until the
 * RUPT LOCK alarm restarted it. */
static void p_clriip(agc *m) { m->cpu.iip = false; }

/* Multiply composites -------------------------------------------------------- */

/* G4-15,16,1 into L1-12,16,15. */
static void p_g2ls(agc *m)
{
    agc_cpu *c = &m->cpu;
    unsigned v = (unsigned)(c->g & AGC_BITS(4, 15)) >> 3;
    v |= c->g & AGC_BIT(16);
    v |= (unsigned)(c->g & 1u) << 14;
    c->l = agc_w((c->l & ~(unsigned)(AGC_BITS(1, 12) | AGC_BITS(15, 16))) | v);
}

/* WL3-16 into A1-14, WL1-2 into L13-14, and A15-16 from G16 or WL16 depending
 * on G1 — the multiply partial-product shift. */
static void p_wals(agc *m)
{
    agc_cpu *c = &m->cpu;
    unsigned wl = c->write_bus;
    unsigned a = wl >> 2;
    unsigned sign = (c->g & 1u) == 0 ? (c->g & AGC_BIT(16)) : (wl & AGC_BIT(16));
    a |= sign >> 1;
    a |= sign;
    c->a = agc_w(a);
    c->l = agc_w((c->l & ~(unsigned)AGC_BITS(13, 14)) | ((wl & AGC_BITS(1, 2)) << 12));
}

static void p_zap(agc *m)
{
    p_ru(m);
    p_g2ls(m);
    p_wals(m);
}

/* ZIP is the multiplier step: what it does depends on the two low bits of L
 * (the next two multiplier bits) and L15. See the table in the memo. */
static void p_zip(agc *m)
{
    agc_cpu *c = &m->cpu;
    unsigned state = ((unsigned)(c->l >> 14) & 1u) << 2;
    state |= c->l & 3u;

    c->mcro = false;
    switch (state) {
    case 0x0: p_wy(m); break;
    case 0x1: p_rb(m); p_wy(m); break;
    case 0x2: p_rb(m); p_wyd(m); break;
    case 0x3: p_rc(m); p_wy(m); p_ci(m); c->mcro = true; break;
    case 0x4: p_rb(m); p_wy(m); break;
    case 0x5: p_rb(m); p_wyd(m); break;
    case 0x6: p_rc(m); p_wy(m); p_ci(m); c->mcro = true; break;
    default:  p_wy(m); c->mcro = true; break;
    }
    p_a2x(m);

    /* L2GD, inlined because it must see the MCRO this ZIP just decided. */
    unsigned g = (unsigned)(c->l & AGC_BITS(1, 14)) << 1;
    g |= c->l & AGC_BIT(16);
    g |= c->mcro ? 1u : 0u;
    c->g = agc_w(g);
}

static void p_l2gd(agc *m)
{
    agc_cpu *c = &m->cpu;
    unsigned g = (unsigned)(c->l & AGC_BITS(1, 14)) << 1;
    g |= c->l & AGC_BIT(16);
    g |= c->mcro ? 1u : 0u;
    c->g = agc_w(g);
}

/* Counter overflow and rate outputs ------------------------------------------- */

/* Positive overflow out of a counter increment. Only some counters care: TIME1
 * carries into TIME2, and TIME3/4/5 raise their interrupts. This is where the
 * AGC's clock actually comes from — TIME1/TIME2 is a 28-bit counter assembled
 * from two 14-bit cells by this pulse. */
static void p_wovr(agc *m)
{
    if (((unsigned)(m->cpu.write_bus & AGC_BITS(15, 16)) >> 14) != 0x1u) {
        return;
    }
    unsigned counter = (unsigned)(m->cpu.s - AGC_COUNTER_BASE);
    switch (counter) {
    case AGC_CNT_TIME1: m->cpu.counters[AGC_CNT_TIME2] |= AGC_COUNT_UP; break;
    case AGC_CNT_TIME3: m->cpu.interrupts[AGC_RUPT_T3RUPT] = true; break;
    case AGC_CNT_TIME4: m->cpu.interrupts[AGC_RUPT_T4RUPT] = true; break;
    case AGC_CNT_TIME5: m->cpu.interrupts[AGC_RUPT_T5RUPT] = true; break;
    default: break;
    }
}

/* DINC's three outcomes: a positive rate pulse, a negative rate pulse, or —
 * when the counter has reached zero — no pulse and a reset of whatever
 * requested the count. TIME6 hitting zero is T6RUPT, the LM's descent-throttle
 * interrupt.
 *
 * PROVISIONAL: POUT and MOUT should also drive the CDU error counters and the
 * gyro torque pulses. There is no CDU/IMU subsystem yet, so those channels are
 * counted but not consumed; see docs/COMPLETION_PLAN.md. */
static void p_pout(agc *m) { (void)m; }
static void p_mout(agc *m) { (void)m; }

static void p_zout(agc *m)
{
    unsigned counter = (unsigned)(m->cpu.s - AGC_COUNTER_BASE);
    switch (counter) {
    case AGC_CNT_TIME6: {
        agc_word v = agc_cpu_read_channel(m, AGC_CH_MISC);
        agc_cpu_write_channel(m, AGC_CH_MISC, agc_w(v & ~(unsigned)AGC_BIT(16)));
        m->cpu.interrupts[AGC_RUPT_T6RUPT] = true;
        break;
    }
    case AGC_CNT_CDUXD:
    case AGC_CNT_CDUYD:
    case AGC_CNT_CDUZD: {
        /* Clear this axis's drive-enable bit in channel 14: bit 15 for X, 14
         * for Y, 13 for Z. */
        unsigned bit = 15u - (counter - AGC_CNT_CDUXD);
        agc_word v = agc_cpu_read_channel(m, AGC_CH_GYRO);
        agc_cpu_write_channel(m, AGC_CH_GYRO,
                              agc_w(v & ~(unsigned)AGC_BIT((int)bit)));
        break;
    }
    default:
        break;
    }
}

/* U2BBK: the adder's bank bits go into EB and FB. Part of the computer-test-set
 * FETCH/STORE sequences, which we implement for completeness even though no
 * CTS is attached. */
static void p_u2bbk(agc *m)
{
    agc_cpu *c = &m->cpu;
    c->eb = agc_w((unsigned)(c->u & AGC_BITS(1, 3)) << 8);
    c->fb = agc_w(c->u & AGC_BITS(11, 15));
    agc_cpu_update_bb(c);
}

/* Dispatch --------------------------------------------------------------------- */

void agc_pulse_apply(agc *m, enum agc_pulse p)
{
    switch (p) {
    case AGC_P_NONE:   break;
    case AGC_P_P1XP10: p_1xp10(m); break;
    case AGC_P_P8XP5:  p_8xp5(m); break;
    case AGC_P_CLRIIP: p_clriip(m); break;
    case AGC_P_A2X:    p_a2x(m); break;
    case AGC_P_B15X:   p_b15x(m); break;
    case AGC_P_CI:     p_ci(m); break;
    case AGC_P_CLXC:   p_clxc(m); break;
    case AGC_P_DVST:   p_dvst(m); break;
    case AGC_P_EXT:    p_ext(m); break;
    case AGC_P_G2LS:   p_g2ls(m); break;
    case AGC_P_KRPT:   p_krpt(m); break;
    case AGC_P_L16:    p_l16(m); break;
    case AGC_P_L2GD:   p_l2gd(m); break;
    case AGC_P_MONEX:  p_monex(m); break;
    case AGC_P_MOUT:   p_mout(m); break;
    case AGC_P_NEACOF: p_neacof(m); break;
    case AGC_P_NEACON: p_neacon(m); break;
    case AGC_P_NISQ:   p_nisq(m); break;
    case AGC_P_PIFL:   p_pifl(m); break;
    case AGC_P_PONEX:  p_ponex(m); break;
    case AGC_P_POUT:   p_pout(m); break;
    case AGC_P_PTWOX:  p_ptwox(m); break;
    case AGC_P_R15:    p_r15(m); break;
    case AGC_P_R1C:    p_r1c(m); break;
    case AGC_P_R6:     p_r6(m); break;
    case AGC_P_RA:     p_ra(m); break;
    case AGC_P_RAD:    p_rad(m); break;
    case AGC_P_RB:     p_rb(m); break;
    case AGC_P_RB1:    p_rb1(m); break;
    case AGC_P_RB1F:   p_rb1f(m); break;
    case AGC_P_RB2:    p_rb2(m); break;
    case AGC_P_RBBK:   p_rbbk(m); break;
    case AGC_P_RC:     p_rc(m); break;
    case AGC_P_RCH:    p_rch(m); break;
    case AGC_P_RG:     p_rg(m); break;
    case AGC_P_RL:     p_rl(m); break;
    case AGC_P_RL10BB: p_rl10bb(m); break;
    case AGC_P_RQ:     p_rq(m); break;
    case AGC_P_RRPA:   p_rrpa(m); break;
    case AGC_P_RSC:    p_rsc(m); break;
    case AGC_P_RSCT:   p_rsct(m); break;
    case AGC_P_RSTRT:  p_rstrt(m); break;
    case AGC_P_RSTSTG: p_rststg(m); break;
    case AGC_P_RU:     p_ru(m); break;
    case AGC_P_RUS:    p_rus(m); break;
    case AGC_P_RZ:     p_rz(m); break;
    case AGC_P_ST1:    p_st1(m); break;
    case AGC_P_ST2:    p_st2(m); break;
    case AGC_P_STAGE:  p_stage(m); break;
    case AGC_P_TL15:   p_tl15(m); break;
    case AGC_P_TMZ:    p_tmz(m); break;
    case AGC_P_TOV:    p_tov(m); break;
    case AGC_P_TPZG:   p_tpzg(m); break;
    case AGC_P_TRSM:   p_trsm(m); break;
    case AGC_P_TSGN:   p_tsgn(m); break;
    case AGC_P_TSGN2:  p_tsgn2(m); break;
    case AGC_P_TSGU:   p_tsgu(m); break;
    case AGC_P_U2BBK:  p_u2bbk(m); break;
    case AGC_P_WA:     p_wa(m); break;
    case AGC_P_WALS:   p_wals(m); break;
    case AGC_P_WB:     p_wb(m); break;
    case AGC_P_WCH:    p_wch(m); break;
    case AGC_P_WG:     p_wg(m); break;
    case AGC_P_WL:     p_wl(m); break;
    case AGC_P_WOVR:   p_wovr(m); break;
    case AGC_P_WQ:     p_wq(m); break;
    case AGC_P_WS:     p_ws(m); break;
    case AGC_P_WSC:    p_wsc(m); break;
    case AGC_P_WY:     p_wy(m); break;
    case AGC_P_WY12:   p_wy12(m); break;
    case AGC_P_WYD:    p_wyd(m); break;
    case AGC_P_WZ:     p_wz(m); break;
    case AGC_P_Z15:    p_z15(m); break;
    case AGC_P_Z16:    p_z16(m); break;
    case AGC_P_ZAP:    p_zap(m); break;
    case AGC_P_ZIP:    p_zip(m); break;
    case AGC_P_ZOUT:   p_zout(m); break;
    case AGC_P_COUNT:  break;
    }
}
