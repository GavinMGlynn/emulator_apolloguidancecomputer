/* The AGC word, and the bit numbering everything else in the core uses.
 *
 * AGC documentation numbers bits from 1 (least significant), so a "bit 16" in
 * the memo is host bit 15.  Every mask in this core is written with AGC_BIT /
 * AGC_BITS so that code can be read straight against AGC4 Memo #9 without
 * mentally subtracting one.  Words are ones' complement: +0 is 000000 and -0 is
 * 177777 (octal, 16 bits), and both compare equal as numbers — which is why the
 * hardware has a dedicated TMZ pulse to test for -0.
 */
#ifndef AGC_WORD_H
#define AGC_WORD_H

#include <stdint.h>

typedef uint16_t agc_word;

/* AGC bit n (1-based) as a mask. */
#define AGC_BIT(n) ((agc_word)(1u << ((n) - 1)))

/* AGC bits lo..hi inclusive, 1-based. */
#define AGC_BITS(lo, hi) ((agc_word)(((1u << ((hi) - (lo) + 1)) - 1u) << ((lo) - 1)))

#define AGC_WORD_MASK  AGC_BITS(1, 16)
#define AGC_ADDR_MASK  AGC_BITS(1, 12) /* the S register is 12 bits */

/* Narrowing helper: the core computes in unsigned int and stores in agc_word,
 * and -Wconversion is on, so every store goes through one explicit cast. */
static inline agc_word agc_w(unsigned int v) { return (agc_word)(v & 0xFFFFu); }

#endif /* AGC_WORD_H */
