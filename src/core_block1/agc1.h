/* A Block I Apollo Guidance Computer.
 *
 * The machine that flew unmanned on Apollo 4 and Apollo 6, and a different
 * computer from the Block II modelled in `src/core` — not an earlier revision
 * of it. The differences that matter to an emulator:
 *
 *   - **Eleven order codes**, decoded from four bits of B, with the extend bit
 *     carried as negative overflow in B rather than by a separate flip-flop.
 *   - **LP, not L**, and a different editing-register set at S = 020-023.
 *   - **Four input and five output registers** reached through S = 4-14, in
 *     place of Block II's sixty-four channels and its READ/WRITE instructions.
 *   - **One bank register** at S = 015, no superbanks, and a memory map with
 *     erasable at 0-1777, fixed-fixed at 2000-5777 and the banked fixed window
 *     at 6000-7777.
 *   - **Memory arrives before T6**, not T4.
 *   - **Twenty counters** at 034 upwards and **six interrupts** vectoring to
 *     2000 + 4n.
 *   - GOJAM starts at **2030**, in stage 2, with bank 1 selected.
 *
 * What is the same is the shape: one subinstruction per memory cycle, twelve
 * timing pulses to the cycle, and a table of control pulses per (timing pulse,
 * branch condition). So this core is stepped the same way — `agc1_tick()` is
 * one timing pulse — and shares the project's discipline, if not its code.
 *
 * **Provenance, stated plainly.** The Block II core is backed by AGC4 Memo #9,
 * by gate netlists and by MIT's own validation rope. This one has none of
 * those: `docs/references/` holds no Block I memo, `ext/agc_simulation` models
 * only Block II, and the Validation suite is a Block II program. Everything
 * here is transcribed from `ext/agcplusplus`'s Block I model and checked for
 * internal consistency. It is worth less than the Block II core, and
 * `docs/PROJECT_STATUS.md` says so.
 */
#ifndef AGC1_H
#define AGC1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "subinst.h"

/* Block I words are the same sixteen bits, ones' complement. */
typedef uint16_t agc1_word;
static inline agc1_word agc1_w(unsigned v) { return (agc1_word)(v & 0177777u); }

#define AGC1_BIT(n) (1u << ((n) - 1))

#define AGC1_TIMEPULSES_PER_MCT 12u

/* Memory map. */
#define AGC1_ERASABLE_WORDS      01000u  /* 0-777 addressable; 0-17 read as 0 */
#define AGC1_ERASABLE_END        01777u
#define AGC1_FIXED_FIXED_START   02000u
#define AGC1_FIXED_FIXED_END     05777u
#define AGC1_FIXED_BANKED_START  06000u
#define AGC1_FIXED_BANKED_END    07777u
#define AGC1_FIXED_BANK_WORDS    02000u
#define AGC1_FIXED_BANKS         034u
#define AGC1_FIXED_WORDS \
    ((AGC1_FIXED_FIXED_END - AGC1_FIXED_FIXED_START + 1u) \
     + AGC1_FIXED_BANK_WORDS * AGC1_FIXED_BANKS)

/* Counters live at 034 upwards; there are twenty. */
#define AGC1_COUNTER_BASE 034u
#define AGC1_COUNTER_COUNT 20u
enum agc1_counter {
    AGC1_CNT_OVCTR = 0, AGC1_CNT_TIME2, AGC1_CNT_TIME1, AGC1_CNT_TIME3,
    AGC1_CNT_TIME4, AGC1_CNT_UPLINK, AGC1_CNT_OUTCR1, AGC1_CNT_OUTCR2,
    AGC1_CNT_PIPAX, AGC1_CNT_PIPAY, AGC1_CNT_PIPAZ, AGC1_CNT_CDUX,
    AGC1_CNT_CDUY, AGC1_CNT_CDUZ, AGC1_CNT_OPTX, AGC1_CNT_OPTY,
    AGC1_CNT_TRKRX, AGC1_CNT_TRKRY, AGC1_CNT_TRKRR, AGC1_CNT_OUTCR3,
};

enum agc1_count_dir { AGC1_COUNT_NONE = 0, AGC1_COUNT_UP, AGC1_COUNT_DOWN };

/* Six interrupts, vectoring to 2000 + 4n. */
#define AGC1_RUPT_COUNT 6u
enum agc1_rupt {
    AGC1_RUPT_T3RUPT = 0, AGC1_RUPT_RPT2, AGC1_RUPT_T4RUPT,
    AGC1_RUPT_KEYRUPT, AGC1_RUPT_UPRUPT, AGC1_RUPT_DOWNRUPT,
};

typedef struct agc1 {
    /* Central registers. LP is Block I's low product, where Block II has L. */
    agc1_word a, q, z, lp, x, y, b, g, s, u;
    agc1_word sq, bank, br;
    uint8_t st, st_next, timepulse;

    /* Four in, five out. */
    agc1_word in[4];
    agc1_word out[5];

    agc1_word write_bus;
    bool carry_in;

    /* Multiply's repeat counter: MP0 loads it with five and CTR walks it down,
     * which is how the MP1 loop knows it has run its six times. */
    uint8_t multiply_counter;

    bool fetch_next_instruction;
    bool extend, extend_next;
    bool inhibit_interrupts, iip, inkl, overflow;
    bool gojam_pending;

    uint8_t counters[AGC1_COUNTER_COUNT];
    bool interrupts[AGC1_RUPT_COUNT];

    const agc1_subinst *subinst;

    agc1_word erasable[AGC1_ERASABLE_WORDS];
    agc1_word fixed[AGC1_FIXED_WORDS];

    uint64_t timepulses;

    bool ignore_counters;
    bool ignore_interrupts;
} agc1;

void agc1_init(agc1 *m);
void agc1_start(agc1 *m);

/* One timing pulse: the three phases, exactly as in the Block II core. */
void agc1_tick(agc1 *m);
void agc1_tick_mct(agc1 *m);

/* Memory. Erasable reads are destructive and rewritten later in the cycle. */
agc1_word agc1_read(agc1 *m, agc1_word address);
void agc1_write(agc1 *m, agc1_word address, agc1_word data);

/* Load a rope image into fixed memory. Returns words read, or -1. */
long agc1_load_rope(agc1 *m, const char *path);

int agc1_format_state(const agc1 *m, char *buf, size_t len);

/* Internal, but declared here so the two translation units agree. */
void agc1_update_adder(agc1 *m);
void agc1_pulse_apply(agc1 *m, enum agc1_pulse p);

#endif /* AGC1_H */
