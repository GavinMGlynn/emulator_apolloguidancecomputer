#include "subinst.h"

/* Mnemonics, for tracing and for the headless frontend's pulse log. Indexed by
 * enum agc_pulse, so the order here must match subinst.h. */
static const char *const names[AGC_P_COUNT] = {
    [AGC_P_NONE]   = "-",
    [AGC_P_P1XP10] = "1xP10",
    [AGC_P_P8XP5]  = "8xP5",
    [AGC_P_CLRIIP] = "CLRIIP",
    [AGC_P_A2X]    = "A2X",     [AGC_P_B15X]  = "B15X",
    [AGC_P_CI]     = "CI",      [AGC_P_CLXC]  = "CLXC",
    [AGC_P_DVST]   = "DVST",    [AGC_P_EXT]   = "EXT",
    [AGC_P_G2LS]   = "G2LS",    [AGC_P_KRPT]  = "KRPT",
    [AGC_P_L16]    = "L16",     [AGC_P_L2GD]  = "L2GD",
    [AGC_P_MONEX]  = "MONEX",   [AGC_P_MOUT]  = "MOUT",
    [AGC_P_NEACOF] = "NEACOF",  [AGC_P_NEACON] = "NEACON",
    [AGC_P_NISQ]   = "NISQ",    [AGC_P_PIFL]  = "PIFL",
    [AGC_P_PONEX]  = "PONEX",   [AGC_P_POUT]  = "POUT",
    [AGC_P_PTWOX]  = "PTWOX",   [AGC_P_R15]   = "R15",
    [AGC_P_R1C]    = "R1C",     [AGC_P_R6]    = "R6",
    [AGC_P_RA]     = "RA",      [AGC_P_RAD]   = "RAD",
    [AGC_P_RB]     = "RB",      [AGC_P_RB1]   = "RB1",
    [AGC_P_RB1F]   = "RB1F",    [AGC_P_RB2]   = "RB2",
    [AGC_P_RBBK]   = "RBBK",    [AGC_P_RC]    = "RC",
    [AGC_P_RCH]    = "RCH",     [AGC_P_RG]    = "RG",
    [AGC_P_RL]     = "RL",      [AGC_P_RL10BB] = "RL10BB",
    [AGC_P_RQ]     = "RQ",      [AGC_P_RRPA]  = "RRPA",
    [AGC_P_RSC]    = "RSC",     [AGC_P_RSCT]  = "RSCT",
    [AGC_P_RSTRT]  = "RSTRT",   [AGC_P_RSTSTG] = "RSTSTG",
    [AGC_P_RU]     = "RU",      [AGC_P_RUS]   = "RUS",
    [AGC_P_RZ]     = "RZ",      [AGC_P_ST1]   = "ST1",
    [AGC_P_ST2]    = "ST2",     [AGC_P_STAGE] = "STAGE",
    [AGC_P_TL15]   = "TL15",    [AGC_P_TMZ]   = "TMZ",
    [AGC_P_TOV]    = "TOV",     [AGC_P_TPZG]  = "TPZG",
    [AGC_P_TRSM]   = "TRSM",    [AGC_P_TSGN]  = "TSGN",
    [AGC_P_TSGN2]  = "TSGN2",   [AGC_P_TSGU]  = "TSGU",
    [AGC_P_U2BBK]  = "U2BBK",   [AGC_P_WA]    = "WA",
    [AGC_P_WALS]   = "WALS",    [AGC_P_WB]    = "WB",
    [AGC_P_WCH]    = "WCH",     [AGC_P_WG]    = "WG",
    [AGC_P_WL]     = "WL",      [AGC_P_WOVR]  = "WOVR",
    [AGC_P_WQ]     = "WQ",      [AGC_P_WS]    = "WS",
    [AGC_P_WSC]    = "WSC",     [AGC_P_WY]    = "WY",
    [AGC_P_WY12]   = "WY12",    [AGC_P_WYD]   = "WYD",
    [AGC_P_WZ]     = "WZ",      [AGC_P_Z15]   = "Z15",
    [AGC_P_Z16]    = "Z16",     [AGC_P_ZAP]   = "ZAP",
    [AGC_P_ZIP]    = "ZIP",     [AGC_P_ZOUT]  = "ZOUT",
};

const char *agc_pulse_name(enum agc_pulse p)
{
    if (p < 0 || p >= AGC_P_COUNT || !names[p]) {
        return "?";
    }
    return names[p];
}
