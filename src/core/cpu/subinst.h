/* Subinstructions and control pulses.
 *
 * A subinstruction occupies exactly one Memory Cycle Time: twelve timing pulses
 * T1-T12. At each timing pulse the cross-point generator asserts zero to five
 * control pulses, selected by the subinstruction (from SQ, ST and EXTEND) and,
 * for some rows, by the two branch registers BR1/BR2.
 *
 * The sequences themselves are in subinst_tables.c, generated from AGC4 Memo #9
 * as corrected by ext/agcplusplus; see tools/gen_subinst_tables.py.
 */
#ifndef AGC_SUBINST_H
#define AGC_SUBINST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The memo says "zero to five control pulses"; we allow one spare slot and the
 * generator asserts the real maximum against this. A pulse list shorter than
 * the array is terminated by AGC_P_NONE, which is why it must be zero. */
#define AGC_MAX_PULSES_PER_TIMEPULSE 6

enum agc_pulse {
    AGC_P_NONE = 0,

    /* Implicit hardware signals with no memo mnemonic. 1xP10 clears G at T1 of
     * DV0 so that RG and RSC do not collide on the write lines at T7 of DV1;
     * 8xP5 sets S bit 12 during divide, redirecting the erasable read; CLRIIP
     * is RESUME dropping the interrupt-in-progress line at T8 of RSM3, without
     * which the machine can never take another interrupt. */
    AGC_P_P1XP10,
    AGC_P_P8XP5,
    AGC_P_CLRIIP,

    AGC_P_A2X, AGC_P_B15X, AGC_P_CI, AGC_P_CLXC, AGC_P_DVST, AGC_P_EXT,
    AGC_P_G2LS, AGC_P_KRPT, AGC_P_L16, AGC_P_L2GD, AGC_P_MONEX, AGC_P_MOUT,
    AGC_P_NEACOF, AGC_P_NEACON, AGC_P_NISQ, AGC_P_PIFL, AGC_P_PONEX,
    AGC_P_POUT, AGC_P_PTWOX, AGC_P_R15, AGC_P_R1C, AGC_P_R6, AGC_P_RA,
    AGC_P_RAD, AGC_P_RB, AGC_P_RB1, AGC_P_RB1F, AGC_P_RB2, AGC_P_RBBK,
    AGC_P_RC, AGC_P_RCH, AGC_P_RG, AGC_P_RL, AGC_P_RL10BB, AGC_P_RQ,
    AGC_P_RRPA, AGC_P_RSC, AGC_P_RSCT, AGC_P_RSTRT, AGC_P_RSTSTG, AGC_P_RU,
    AGC_P_RUS, AGC_P_RZ, AGC_P_ST1, AGC_P_ST2, AGC_P_STAGE, AGC_P_TL15,
    AGC_P_TMZ, AGC_P_TOV, AGC_P_TPZG, AGC_P_TRSM, AGC_P_TSGN, AGC_P_TSGN2,
    AGC_P_TSGU, AGC_P_U2BBK, AGC_P_WA, AGC_P_WALS, AGC_P_WB, AGC_P_WCH,
    AGC_P_WG, AGC_P_WL, AGC_P_WOVR, AGC_P_WQ, AGC_P_WS, AGC_P_WSC,
    AGC_P_WY, AGC_P_WY12, AGC_P_WYD, AGC_P_WZ, AGC_P_Z15, AGC_P_Z16,
    AGC_P_ZAP, AGC_P_ZIP, AGC_P_ZOUT,

    AGC_P_COUNT
};

/* One row of a pulse sequence: "at timing pulse `tp`, if the branch registers
 * satisfy (br & br_mask) == br_value, assert these pulses in this order".
 *
 * The order is the *execution* order, which is not always the order the memo
 * prints: PIFL must precede the WYD it gates, and TSGU must precede the CLXC or
 * RB1F that reads its result. Those reorderings are recorded in
 * tools/oracle/FINDINGS.md. */
typedef struct agc_pulse_row {
    uint8_t tp;       /* 1..12 */
    uint8_t br_mask;  /* which of BR1 (0x2) / BR2 (0x1) this row tests */
    uint8_t br_value;
    uint8_t pulse[AGC_MAX_PULSES_PER_TIMEPULSE];
} agc_pulse_row;

typedef struct agc_subinst {
    const char *name;
    uint8_t stage;    /* required value of ST */
    bool extend;      /* required value of the EXTEND flip-flop */
    uint8_t sq_mask;  /* which SQ bits the opcode is decoded from */
    uint8_t sq_value;
    const agc_pulse_row *rows;
    uint8_t row_count;
} agc_subinst;

/* Subinstructions reachable by decoding SQ/ST/EXTEND, in priority order. */
extern const agc_subinst agc_subinst_table[];
extern const size_t agc_subinst_count;

/* Involuntary sequences, injected by priority control rather than decoded. */
extern const agc_subinst agc_subinst_pinc;
extern const agc_subinst agc_subinst_minc;
extern const agc_subinst agc_subinst_pcdu;
extern const agc_subinst agc_subinst_mcdu;
extern const agc_subinst agc_subinst_dinc;
/* The serial shift-in sequences: SHINC shifts a zero into the counter, SHANC a
 * one (its extra CI at T5 is the whole difference). Both hold the SHIFT line,
 * which is what stops WYD rotating bit 16 back into bit 1. */
extern const agc_subinst agc_subinst_shinc;
extern const agc_subinst agc_subinst_shanc;

/* Human-readable pulse mnemonic, for tracing. */
const char *agc_pulse_name(enum agc_pulse p);

#endif /* AGC_SUBINST_H */
