#include "counters.h"

enum agc_counter_kind agc_counter_kind(unsigned counter)
{
    switch (counter) {
    case AGC_CNT_TIME1:
    case AGC_CNT_TIME2:
    case AGC_CNT_TIME3:
    case AGC_CNT_TIME4:
    case AGC_CNT_TIME5:
        return AGC_CK_PINC;
    case AGC_CNT_TIME6:
        return AGC_CK_DINC;
    case AGC_CNT_CDUX:
    case AGC_CNT_CDUY:
    case AGC_CNT_CDUZ:
    case AGC_CNT_TRN:
    case AGC_CNT_SHFT:
        return AGC_CK_PCDU_MCDU;
    case AGC_CNT_PIPAX:
    case AGC_CNT_PIPAY:
    case AGC_CNT_PIPAZ:
    case AGC_CNT_BMAGX:
    case AGC_CNT_BMAGY:
    case AGC_CNT_BMAGZ:
        return AGC_CK_PINC_MINC;
    case AGC_CNT_INLINK:
    case AGC_CNT_RNRAD:
        return AGC_CK_SHINC_SHANC;
    case AGC_CNT_GYROD:
    case AGC_CNT_CDUXD:
    case AGC_CNT_CDUYD:
    case AGC_CNT_CDUZD:
    case AGC_CNT_TRUND:
    case AGC_CNT_SHAFTD:
    case AGC_CNT_THRSTD:
    case AGC_CNT_EMSD:
        return AGC_CK_DINC;
    case AGC_CNT_OTLNK:
    default:
        return AGC_CK_SHINC;
    }
}
