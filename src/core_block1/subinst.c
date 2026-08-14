#include "subinst.h"

/* Human-readable pulse mnemonics, for tracing. */
static const char *const names[AGC1_P_COUNT] = {
    [AGC1_P_NONE] = "-",
    [AGC1_P_RA] = "RA", [AGC1_P_RB] = "RB", [AGC1_P_RB14] = "RB14",
    [AGC1_P_RC] = "RC", [AGC1_P_RG] = "RG", [AGC1_P_RLP] = "RLP",
    [AGC1_P_RP2] = "RP2", [AGC1_P_RQ] = "RQ", [AGC1_P_RSB] = "RSB",
    [AGC1_P_RSC] = "RSC", [AGC1_P_RSCT] = "RSCT", [AGC1_P_RU] = "RU",
    [AGC1_P_RZ] = "RZ", [AGC1_P_R1] = "R1", [AGC1_P_R1C] = "R1C",
    [AGC1_P_R2] = "R2", [AGC1_P_R22] = "R22", [AGC1_P_R24] = "R24",
    [AGC1_P_RRPA] = "RRPA",
    [AGC1_P_WA] = "WA", [AGC1_P_WALP] = "WALP", [AGC1_P_WB] = "WB",
    [AGC1_P_WG] = "WG", [AGC1_P_WLP] = "WLP", [AGC1_P_WP] = "WP",
    [AGC1_P_WP2] = "WP2", [AGC1_P_WQ] = "WQ", [AGC1_P_WS] = "WS",
    [AGC1_P_WSC] = "WSC", [AGC1_P_WX] = "WX", [AGC1_P_WY] = "WY",
    [AGC1_P_WZ] = "WZ",
    [AGC1_P_CI] = "CI", [AGC1_P_CLG] = "CLG", [AGC1_P_CTR] = "CTR",
    [AGC1_P_GP] = "GP", [AGC1_P_TP] = "TP",
    [AGC1_P_WOVC] = "WOVC", [AGC1_P_WOVI] = "WOVI", [AGC1_P_WOVR] = "WOVR",
    [AGC1_P_TMZ] = "TMZ", [AGC1_P_TOV] = "TOV", [AGC1_P_TSGN] = "TSGN",
    [AGC1_P_TSGN2] = "TSGN2", [AGC1_P_TRSM] = "TRSM",
    [AGC1_P_ST1] = "ST1", [AGC1_P_ST2] = "ST2", [AGC1_P_NISQ] = "NISQ",
    [AGC1_P_KRPT] = "KRPT",
    [AGC1_P_SETMPCTR] = "SETMPCTR", [AGC1_P_CLRIIP] = "CLRIIP",
};

const char *agc1_pulse_name(enum agc1_pulse p)
{
    return (p < AGC1_P_COUNT && names[p]) ? names[p] : "?";
}
