/* Priority control: counter cells and program interrupts.
 *
 * Reference: Information Series #8, "Priority Control".
 *
 * The AGC has no DMA. Instead, 29 erasable locations (0024-0060) are *counters*
 * with a hardware request cell each. When a peripheral pulses a counter, the
 * sequence generator steals one MCT from the program at the next T12 and runs
 * an involuntary sequence (PINC, MINC, PCDU, MCDU, DINC, SHINC, SHANC) that
 * increments the cell in place. Counters outrank interrupts, and both outrank
 * the program. Under enough counter traffic the program stops making progress —
 * which is precisely the Apollo 11 1201/1202 mechanism, and is emergent here
 * rather than modelled.
 */
#ifndef AGC_COUNTERS_H
#define AGC_COUNTERS_H

#include "../agc_word.h"

/* Counter index i lives at erasable address 0024 + i. */
#define AGC_COUNTER_BASE 024u
#define AGC_COUNTER_COUNT 28u

enum agc_counter {
    AGC_CNT_TIME2 = 0, AGC_CNT_TIME1, AGC_CNT_TIME3, AGC_CNT_TIME4,
    AGC_CNT_TIME5, AGC_CNT_TIME6, AGC_CNT_CDUX, AGC_CNT_CDUY,
    AGC_CNT_CDUZ, AGC_CNT_TRN, AGC_CNT_SHFT, AGC_CNT_PIPAX,
    AGC_CNT_PIPAY, AGC_CNT_PIPAZ, AGC_CNT_BMAGX, AGC_CNT_BMAGY,
    AGC_CNT_BMAGZ, AGC_CNT_INLINK, AGC_CNT_RNRAD, AGC_CNT_GYROD,
    AGC_CNT_CDUXD, AGC_CNT_CDUYD, AGC_CNT_CDUZD, AGC_CNT_TRUND,
    AGC_CNT_SHAFTD, AGC_CNT_THRSTD, AGC_CNT_EMSD, AGC_CNT_OTLNK,
};

/* A request cell holds a direction, not a count: two pulses arriving before the
 * cell is serviced are one increment, and the "freak accident" of both
 * directions at once is representable because the hardware allows it. */
enum agc_count_dir {
    AGC_COUNT_NONE = 0,
    AGC_COUNT_UP   = 1,
    AGC_COUNT_DOWN = 2,
};

/* Which involuntary sequence a given counter runs. */
enum agc_counter_kind {
    AGC_CK_PINC,      /* up only */
    AGC_CK_PINC_MINC, /* up or down */
    AGC_CK_DINC,      /* toward zero, emitting a rate pulse */
    AGC_CK_PCDU_MCDU, /* ones'-complement CDU angle counters */
    AGC_CK_SHINC,     /* shift in, serial input */
    AGC_CK_SHINC_SHANC,
};

enum agc_counter_kind agc_counter_kind(unsigned counter);

/* Interrupt vectors live at 04000 + 4n, in priority order. */
#define AGC_RUPT_COUNT 11u

enum agc_rupt {
    AGC_RUPT_GO = 0,   /* not a vector: GOJAM lands at 04000 by other means */
    AGC_RUPT_T6RUPT,
    AGC_RUPT_T5RUPT,
    AGC_RUPT_T3RUPT,
    AGC_RUPT_T4RUPT,
    AGC_RUPT_KEYRUPT1,
    AGC_RUPT_KEYRUPT2, /* shares its vector with MARKRUPT */
    AGC_RUPT_UPRUPT,
    AGC_RUPT_DOWNRUPT,
    AGC_RUPT_RADARRUPT,
    AGC_RUPT_RUPT10,
};

#define AGC_RUPT_MARKRUPT AGC_RUPT_KEYRUPT2

#endif /* AGC_COUNTERS_H */
