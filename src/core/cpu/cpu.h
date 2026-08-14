/* The Block II central processor and sequence generator.
 *
 * References: Information Series #3 (central processor) and #7 (sequence
 * generator); AGC4 Memo #9 for the pulse sequences themselves.
 *
 * The machine cycle here is the *timing pulse*, not the instruction. One
 * agc_cpu_tick() is one of the twelve pulses in a Memory Cycle Time, and it
 * runs in three phases, matching the hardware's own division of labour:
 *
 *   before  — things the memory and priority-control logic do between pulses:
 *             the erasable read (T5), the erasable rewrite (T10), GOJAM, and
 *             the counter-request scan at T1.
 *   during  — the control pulses this subinstruction asserts at this pulse.
 *   after   — clear the write lines, and at T12 (or T3 in divide) advance the
 *             stage counter and select the next subinstruction.
 *
 * Splitting it this way is not cosmetic: the write lines are a wired-OR bus
 * that exists only within a single timing pulse, and a read pulse issued in the
 * same pulse as a write pulse must see the value the read placed there.
 */
#ifndef AGC_CPU_H
#define AGC_CPU_H

#include <stdbool.h>
#include <stdint.h>

#include "../agc_word.h"
#include "../io/counters.h"
#include "subinst.h"

struct agc;

typedef struct agc_cpu {
    /* Central registers. A, L, Q, Z are program-visible; B/C is the buffer
     * (C is B inverted, read by the RC pulse); G is the memory buffer; S is the
     * 12-bit address register; X, Y and U are the adder's two inputs and its
     * output. */
    agc_word a, l, q, z, g, b, s;
    agc_word x, y, u;

    /* Bank registers. EB is erasable (bits 9-11), FB is fixed (bits 11-15),
     * BB is the two of them packed into one word, FEXT is the superbank. */
    agc_word eb, fb, bb, fext;

    /* Sequence generator state. SQ holds the order code; ST is the stage
     * counter (which subinstruction of a multi-MCT instruction we are in);
     * BR holds the two branch registers as (BR1 << 1) | BR2. */
    agc_word sq;
    uint8_t st, st_next, br;

    /* The write lines: a wired-OR bus, cleared after every timing pulse. */
    agc_word write_bus;

    /* Adder control. */
    bool explicit_carry; /* set by CI, cleared by any WY/WYD/WY12 */
    bool no_eac;         /* NEACON..NEACOF inhibits the end-around carry */
    bool mp3a;           /* the MP3 decode line, a second, independent inhibit */

    /* Address of the erasable word read this cycle, held so the rewrite before
     * T10 goes back to the right place even if S has since changed. Zero means
     * "no erasable cycle in progress" — address 0 is register A, never core. */
    agc_word s_writeback;

    /* Flip-flops and latches, all named for the signal they carry. */
    bool extend, extend_next;
    bool fetch_next_instruction; /* NISQ seen: load SQ from B at T12 */
    bool inhibit_interrupts;     /* INHINT..RELINT */
    bool iip;                    /* interrupt in progress */
    bool pseudo;                 /* the word just fetched was INHINT/RELINT/EXTEND */
    bool mcro;                   /* multiply carry-round, set by ZIP */
    bool pifl;                   /* divide: block the end-around carry on WYD */
    bool shinc;                  /* a shift-in sequence is running */
    bool dv;                     /* a divide is running: MCTs end at T3 */
    bool inkl;                   /* this MCT was stolen by a counter request */
    bool channel_access;         /* this MCT touched an I/O channel, not memory */
    bool restart;                /* the RESTART lamp latch */
    bool night_watchman;         /* erasable 067 was touched since the last check */
    uint8_t dv_stage;            /* grey-coded divide stage, advanced by DVST */

    /* Priority control. */
    uint8_t counters[AGC_COUNTER_COUNT]; /* enum agc_count_dir per cell */
    bool interrupts[AGC_RUPT_COUNT];

    /* Where we are. */
    uint8_t timepulse; /* 1..12 */
    const agc_subinst *subinst;
    bool gojam_pending;
} agc_cpu;

/* Bring the machine up: clear state, set the inverted channels, and inject the
 * GOJ1 sequence that a real AGC runs out of reset. */
void agc_cpu_start(struct agc *m);

/* Advance exactly one timing pulse. */
void agc_cpu_tick(struct agc *m);

/* Request a GOJAM (hardware restart). It takes effect before the next T1. */
void agc_cpu_queue_gojam(agc_cpu *cpu);

/* Adder: U = X + Y, plus the explicit carry from CI and the end-around carry,
 * recomputed after every pulse that touches X, Y or the carry controls. */
void agc_cpu_update_adder(agc_cpu *cpu);

/* BB is the packed view of EB and FB; the three are kept coherent. */
void agc_cpu_update_bb(agc_cpu *cpu);
void agc_cpu_update_eb_fb(agc_cpu *cpu);

/* Channel access with the L/Q/FEXT aliasing and the bit-15/16 fixups the
 * hardware applies (channels are 15-bit; bit 16 mirrors bit 15). */
agc_word agc_cpu_read_channel(struct agc *m, unsigned n);
void     agc_cpu_write_channel(struct agc *m, unsigned n, agc_word v);

#endif /* AGC_CPU_H */
