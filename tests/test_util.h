/* Shared helpers for the Unity suites.
 *
 * Tests drive the real core: they poke a few words of rope into fixed memory,
 * run whole Memory Cycle Times, and assert on register and memory state. There
 * is no instruction-level shortcut to test against, by design.
 */
#ifndef AGC_TEST_UTIL_H
#define AGC_TEST_UTIL_H

#include <stdlib.h>

#include "agc.h"

/* Fixed-fixed banks 2 and 3 are addressed directly, so a program written at
 * 04000 is what GOJAM jumps to. */
#define TEST_PROGRAM_ORIGIN 04000u

/* Startup costs two MCTs before the first instruction runs: GOJ1 sets S to
 * 04000, then a TC0 fetches the word there. Instruction k then executes at
 * MCT 3 + (MCTs consumed by instructions 0..k-1), each single-MCT instruction
 * costing one MCT plus one STD2 fetch. */
#define TEST_STARTUP_MCTS 2u

/* Write a 15-bit word into fixed memory in the physical rope layout, computing
 * its parity so the fetch does not trip the PARITY FAIL alarm. */
static inline void test_put_fixed(agc *m, unsigned addr, unsigned data)
{
    unsigned raw = (data & 037777u) | ((data & 040000u) << 1);
    unsigned bits = 0, v = raw;
    while (v) {
        bits ^= v & 1u;
        v >>= 1;
    }
    if (bits == 0) {
        raw |= 040000u; /* parity bit sits in position 15 */
    }
    agc_memory_write_fixed_raw(&m->mem, addr, agc_w(raw));
}

/* Bring up a machine with `words` of program at 04000. */
static inline void test_boot(agc *m, const unsigned *words, unsigned count)
{
    agc_init(m);
    for (unsigned i = 0; i < count; ++i) {
        test_put_fixed(m, TEST_PROGRAM_ORIGIN + i, words[i]);
    }
    /* Re-run the power-on sequence now that the rope is in place. */
    agc_cpu_start(m);
}

static inline void test_run_mcts(agc *m, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        agc_tick_mct(m);
    }
}

/* Instruction encodings. The opcode is bits 13-15; extracodes need a preceding
 * EXTEND (TC 6) and set the extend flip-flop. */
#define OP(code, addr) (((unsigned)(code) << 12) | ((unsigned)(addr) & 07777u))
#define QC(code, quarter, addr) \
    (((unsigned)(code) << 12) | ((unsigned)(quarter) << 10) | ((unsigned)(addr) & 01777u))

#define I_TC(a)     OP(0, a)
#define I_CCS(a)    QC(1, 0, a)
/* TCF is order code 001 with a non-zero quarter code — but the quarter code
 * bits *are* address bits 11-12, so any fixed address (>= 02000) encodes it and
 * anything below would be a CCS. There is nothing extra to OR in. */
#define I_TCF(a)    OP(1, a)
#define I_DAS(a)    QC(2, 0, a)
#define I_LXCH(a)   QC(2, 1, a)
#define I_INCR(a)   QC(2, 2, a)
#define I_ADS(a)    QC(2, 3, a)
#define I_CA(a)     OP(3, a)
#define I_CS(a)     OP(4, a)
#define I_INDEX(a)  QC(5, 0, a)
#define I_DXCH(a)   QC(5, 1, a)
#define I_TS(a)     QC(5, 2, a)
#define I_XCH(a)    QC(5, 3, a)
#define I_AD(a)     OP(6, a)
#define I_MASK(a)   OP(7, a)

#define I_EXTEND    I_TC(6)
#define I_INHINT    I_TC(4)
#define I_RELINT    I_TC(3)

/* Extracodes (must follow EXTEND). */
#define I_DV(a)     QC(1, 0, a)
#define I_MSU(a)    QC(2, 0, a)
#define I_QXCH(a)   QC(2, 1, a)
#define I_AUG(a)    QC(2, 2, a)
#define I_DIM(a)    QC(2, 3, a)
#define I_DCA(a)    OP(3, a)
#define I_DCS(a)    OP(4, a)
#define I_SU(a)     QC(6, 0, a)
#define I_MP(a)     OP(7, a)

#endif /* AGC_TEST_UTIL_H */
