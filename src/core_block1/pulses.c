/* The Block I control pulses.
 *
 * Fifty-one of them, against Block II's seventy-odd, and several with no Block
 * II counterpart at all: the editing registers are reached through WG rather
 * than through named shift pulses, WALP and WLP move the low product a bit at a
 * time during multiply, and CTR is the multiply loop's own counter.
 *
 * Transcribed from ext/agcplusplus's Block I model. Where that model marks a
 * pulse TODO — RP2, WP, WP2, TP, GP, the parity machinery — it is left as a
 * documented no-op here too rather than invented, and named in
 * docs/PROJECT_STATUS.md.
 */
#include "agc1.h"


#define BIT(n) AGC1_BIT(n)
#define BITS(a, b) (((1u << ((b) - (a) + 1)) - 1u) << ((a) - 1))

static unsigned sign_bits(agc1_word w)
{
    return (unsigned)((w >> 14) & 3u); /* bits 15 and 16 */
}

void agc1_update_adder(agc1 *m)
{
    unsigned sum = (unsigned)m->x + (unsigned)m->y;
    unsigned carry = m->carry_in ? 1u : 0u;
    /* Ones' complement end-around carry, as on Block II. */
    carry |= (sum >> 16) & 1u;
    m->u = agc1_w(sum + carry);
}

/* --- the editing registers -------------------------------------------------
 * Block I edits in G on the way through, selected by the address in S, which is
 * why there are no named shift pulses: writing to 020-023 *is* the shift. */

static agc1_word cycle_right(agc1_word v)
{
    unsigned bottom = v & 1u;
    unsigned top = v & BIT(16);
    unsigned t = (v & ~(BIT(15) | BIT(16))) >> 1;
    t |= bottom << 15;
    t |= top >> 2;
    return agc1_w(t);
}

static agc1_word cycle_right_lp(agc1_word v)
{
    unsigned bottom = v & 1u;
    unsigned t = (v & ~(BIT(15) | BIT(16))) >> 1;
    t |= bottom << 15;
    t |= bottom << 14;
    return agc1_w(t);
}

static agc1_word shift_right(agc1_word v)
{
    unsigned top = v & BIT(16);
    return agc1_w((v >> 1) | top);
}

static agc1_word cycle_left(agc1_word v)
{
    unsigned top = v & BIT(16);
    unsigned b14 = v & BIT(14);
    unsigned t = (v & ~(BIT(14) | BIT(15))) << 1;
    t |= b14 << 2;
    t |= top >> 15;
    return agc1_w(t);
}

static agc1_word shift_left(agc1_word v)
{
    unsigned top = v & BIT(16);
    unsigned t = (v & ~(BIT(14) | BIT(15))) << 1;
    t |= top;
    t |= top >> 15;
    return agc1_w(t);
}

/* --- reads ----------------------------------------------------------------- */

static void p_ra(agc1 *m) { m->write_bus |= m->a; }
static void p_rb(agc1 *m) { m->write_bus |= m->b; }
static void p_rb14(agc1 *m) { m->write_bus |= BIT(14); }
static void p_rc(agc1 *m) { m->write_bus = agc1_w(m->write_bus | (unsigned)~m->b); }
static void p_rlp(agc1 *m) { m->write_bus |= m->lp; }
static void p_rq(agc1 *m) { m->write_bus |= m->q; }
static void p_rsb(agc1 *m) { m->write_bus |= BIT(16); }
static void p_ru(agc1 *m) { m->write_bus |= m->u; }
static void p_rz(agc1 *m) { m->write_bus |= m->z; }
static void p_r1(agc1 *m) { m->write_bus |= 1u; }
static void p_r1c(agc1 *m) { m->write_bus |= 0177776u; }
static void p_r2(agc1 *m) { m->write_bus |= 2u; }
static void p_r22(agc1 *m) { m->write_bus |= 022u; }
static void p_r24(agc1 *m) { m->write_bus |= 024u; }

static void p_rg(agc1 *m)
{
    /* G holds the sign in bit 16 and parity in 15; reading puts the sign back
     * into 15 so the adder sees a well-formed word. */
    unsigned t = m->g & ~(unsigned)BIT(15);
    t |= (t & BIT(16)) >> 1;
    m->write_bus |= agc1_w(t);
}

/* The central store, which on Block I includes the input and output registers
 * and the bank register — there are no channel instructions. */
static void p_rsc(agc1 *m)
{
    switch (m->s) {
    case 0: m->write_bus |= m->a; break;
    case 1: m->write_bus |= m->q; break;
    case 2: m->write_bus |= m->z; break;
    case 3: m->write_bus |= m->lp; break;
    case 4: case 5: case 6: case 7:
        m->write_bus |= m->in[m->s - 4u];
        break;
    case 010: case 011: case 012: case 013: case 014:
        m->write_bus |= m->out[m->s - 010u];
        break;
    case 015:
        m->write_bus |= agc1_w((unsigned)m->bank << 10);
        /* Duplicate bit 15 into 16 so the bank does not read as overflow. */
        m->write_bus |= agc1_w((m->write_bus & BIT(15)) << 1);
        break;
    default: break;
    }
}

static void p_rsct(agc1 *m)
{
    for (unsigned c = 0; c < AGC1_COUNTER_COUNT; ++c) {
        if (m->counters[c] != AGC1_COUNT_NONE) {
            m->write_bus |= agc1_w(c + AGC1_COUNTER_BASE);
            return;
        }
    }
}

static void p_rrpa(agc1 *m)
{
    for (unsigned i = 0; i < AGC1_RUPT_COUNT; ++i) {
        if (m->interrupts[i]) {
            m->write_bus |= agc1_w(02000u + i * 4u);
            return;
        }
    }
}

/* --- writes ---------------------------------------------------------------- */

static void p_wa(agc1 *m) { m->a = m->write_bus; }
static void p_wb(agc1 *m) { m->b = m->write_bus; }
static void p_wq(agc1 *m) { m->q = m->write_bus; }
static void p_wz(agc1 *m) { m->z = m->write_bus; }
static void p_ws(agc1 *m) { m->s = agc1_w(m->write_bus & BITS(1, 12)); }
static void p_wlp(agc1 *m) { m->lp = cycle_right_lp(m->write_bus); }

static void p_walp(agc1 *m)
{
    /* Multiply's shift: the accumulator moves right and the bit falling off the
     * bottom goes into LP's bit 14. */
    unsigned bit_one = m->write_bus & 1u;
    m->a = shift_right(m->write_bus);
    m->lp = agc1_w((m->lp & ~(unsigned)BIT(14)) | (bit_one << 13));
}

static void p_wg(agc1 *m)
{
    switch (m->s) {
    case 020: m->g = cycle_right(m->write_bus); break;
    case 021: m->g = shift_right(m->write_bus); break;
    case 022: m->g = cycle_left(m->write_bus); break;
    case 023: m->g = shift_left(m->write_bus); break;
    default:  m->g = m->write_bus; break;
    }
}

static void p_wx(agc1 *m) { m->x |= m->write_bus; agc1_update_adder(m); }

static void p_wy(agc1 *m)
{
    m->x = 0;
    m->y = m->write_bus;
    m->carry_in = false;
    agc1_update_adder(m);
}

static void p_wsc(agc1 *m)
{
    switch (m->s) {
    case 0: p_wa(m); break;
    case 1: p_wq(m); break;
    case 2: p_wz(m); break;
    case 3: p_wlp(m); break;
    case 4: case 5: case 6: case 7:
        /* Reading an input register clears it; writing one does nothing else. */
        m->in[m->s - 4u] = 0;
        break;
    case 010: case 011: case 012: case 013: case 014:
        m->out[m->s - 010u] = m->write_bus;
        break;
    case 015:
        m->bank = agc1_w((m->write_bus & BITS(11, 15)) >> 10);
        break;
    default: break;
    }
}

/* --- tests and sequence control -------------------------------------------- */

static void p_ci(agc1 *m) { m->carry_in = true; agc1_update_adder(m); }
static void p_clg(agc1 *m) { m->g = 0; }
static void p_st1(agc1 *m) { m->st_next |= 1u; }
static void p_st2(agc1 *m) { m->st_next |= 2u; }
static void p_nisq(agc1 *m) { m->fetch_next_instruction = true; }
static void p_clriip(agc1 *m) { m->iip = false; }
static void p_setmpctr(agc1 *m) { m->multiply_counter = 5; }

static void p_ctr(agc1 *m)
{
    /* The multiply loop's own counter: when it runs out, stage 2 ends MP1. */
    if (m->multiply_counter == 0) {
        p_st2(m);
    } else {
        m->multiply_counter--;
    }
}

static void p_tsgn(agc1 *m)
{
    unsigned s = sign_bits(m->write_bus);
    m->br = (agc1_word)((s == 0b10 || s == 0b11) ? (m->br | 0b10u) : (m->br & 0b01u));
}

static void p_tsgn2(agc1 *m)
{
    unsigned s = sign_bits(m->write_bus);
    m->br = (agc1_word)((s == 0b10 || s == 0b11) ? (m->br | 0b01u) : (m->br & 0b10u));
}

static void p_tmz(agc1 *m)
{
    m->br = (agc1_word)(m->write_bus == 0177777u ? (m->br | 0b01u) : (m->br & 0b10u));
}

static void p_tov(agc1 *m)
{
    unsigned s = sign_bits(m->write_bus);
    m->br = (agc1_word)((s == 0b01 || s == 0b10) ? s : 0u);
}

static void p_trsm(agc1 *m)
{
    /* RESUME is recognised by its address, as on Block II — a different one. */
    if (m->s == 025u) {
        p_st2(m);
    }
}

static void p_krpt(agc1 *m)
{
    for (unsigned i = 0; i < AGC1_RUPT_COUNT; ++i) {
        if (m->interrupts[i]) {
            m->interrupts[i] = false;
            return;
        }
    }
}

static void p_wovc(agc1 *m)
{
    unsigned s = sign_bits(m->write_bus);
    if (s == 0b01) {
        m->counters[AGC1_CNT_OVCTR] = AGC1_COUNT_UP;
    } else if (s == 0b10) {
        m->counters[AGC1_CNT_OVCTR] = AGC1_COUNT_DOWN;
    }
}

static void p_wovi(agc1 *m)
{
    unsigned s = sign_bits(m->write_bus);
    if (s == 0b01 || s == 0b10) {
        m->overflow = true;
    }
}

static void p_wovr(agc1 *m)
{
    unsigned counter = (unsigned)(m->s - AGC1_COUNTER_BASE);
    if (counter >= AGC1_COUNTER_COUNT) {
        return;
    }
    if (sign_bits(m->write_bus) == 0b01) {
        switch (counter) {
        case AGC1_CNT_TIME1: m->counters[AGC1_CNT_TIME2] = AGC1_COUNT_UP; break;
        case AGC1_CNT_TIME3: m->interrupts[AGC1_RUPT_T3RUPT] = true; break;
        case AGC1_CNT_TIME4: m->interrupts[AGC1_RUPT_T4RUPT] = true; break;
        default: break;
        }
    }
    m->counters[counter] = AGC1_COUNT_NONE;
}

/* Left as no-ops, exactly as the reference model leaves them: the parity
 * generator and the P/P2 registers it feeds. Named here so that "not
 * implemented" is visible in the source rather than inferred from silence. */
static void p_gp(agc1 *m) { (void)m; }
static void p_tp(agc1 *m) { (void)m; }
static void p_wp(agc1 *m) { (void)m; }
static void p_wp2(agc1 *m) { (void)m; }
static void p_rp2(agc1 *m) { (void)m; }

void agc1_pulse_apply(agc1 *m, enum agc1_pulse p)
{
    switch (p) {
    case AGC1_P_NONE: break;
    case AGC1_P_RA: p_ra(m); break;
    case AGC1_P_RB: p_rb(m); break;
    case AGC1_P_RB14: p_rb14(m); break;
    case AGC1_P_RC: p_rc(m); break;
    case AGC1_P_RG: p_rg(m); break;
    case AGC1_P_RLP: p_rlp(m); break;
    case AGC1_P_RP2: p_rp2(m); break;
    case AGC1_P_RQ: p_rq(m); break;
    case AGC1_P_RSB: p_rsb(m); break;
    case AGC1_P_RSC: p_rsc(m); break;
    case AGC1_P_RSCT: p_rsct(m); break;
    case AGC1_P_RU: p_ru(m); break;
    case AGC1_P_RZ: p_rz(m); break;
    case AGC1_P_R1: p_r1(m); break;
    case AGC1_P_R1C: p_r1c(m); break;
    case AGC1_P_R2: p_r2(m); break;
    case AGC1_P_R22: p_r22(m); break;
    case AGC1_P_R24: p_r24(m); break;
    case AGC1_P_RRPA: p_rrpa(m); break;
    case AGC1_P_WA: p_wa(m); break;
    case AGC1_P_WALP: p_walp(m); break;
    case AGC1_P_WB: p_wb(m); break;
    case AGC1_P_WG: p_wg(m); break;
    case AGC1_P_WLP: p_wlp(m); break;
    case AGC1_P_WP: p_wp(m); break;
    case AGC1_P_WP2: p_wp2(m); break;
    case AGC1_P_WQ: p_wq(m); break;
    case AGC1_P_WS: p_ws(m); break;
    case AGC1_P_WSC: p_wsc(m); break;
    case AGC1_P_WX: p_wx(m); break;
    case AGC1_P_WY: p_wy(m); break;
    case AGC1_P_WZ: p_wz(m); break;
    case AGC1_P_CI: p_ci(m); break;
    case AGC1_P_CLG: p_clg(m); break;
    case AGC1_P_CTR: p_ctr(m); break;
    case AGC1_P_GP: p_gp(m); break;
    case AGC1_P_TP: p_tp(m); break;
    case AGC1_P_WOVC: p_wovc(m); break;
    case AGC1_P_WOVI: p_wovi(m); break;
    case AGC1_P_WOVR: p_wovr(m); break;
    case AGC1_P_TMZ: p_tmz(m); break;
    case AGC1_P_TOV: p_tov(m); break;
    case AGC1_P_TSGN: p_tsgn(m); break;
    case AGC1_P_TSGN2: p_tsgn2(m); break;
    case AGC1_P_TRSM: p_trsm(m); break;
    case AGC1_P_ST1: p_st1(m); break;
    case AGC1_P_ST2: p_st2(m); break;
    case AGC1_P_NISQ: p_nisq(m); break;
    case AGC1_P_KRPT: p_krpt(m); break;
    case AGC1_P_SETMPCTR: p_setmpctr(m); break;
    case AGC1_P_CLRIIP: p_clriip(m); break;
    case AGC1_P_COUNT: break;
    }
}
