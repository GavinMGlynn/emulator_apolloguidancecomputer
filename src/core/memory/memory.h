/* Erasable (coincident-current core) and fixed (core rope) memory.
 *
 * References: Information Series #4 (erasable) and #10 (fixed) in
 * docs/references/agc-information-series/.
 *
 * Erasable reads are destructive and are followed by a rewrite; the core models
 * that literally, which is what fixes the read at T4/T5 and the writeback at
 * T10 in cpu.c rather than making them arbitrary.
 *
 * Fixed words are stored in the *physical rope layout* produced by
 * `yaYUL --hardware`: data bits 1-14 in positions 1-14, parity in position 15,
 * data bit 15 in position 16.  A rope word therefore has odd parity across all
 * 16 stored bits, exactly as the sense amplifiers see it.
 */
#ifndef AGC_MEMORY_H
#define AGC_MEMORY_H

#include <stdbool.h>
#include <stddef.h>

#include "../agc_word.h"

#define AGC_ERASABLE_WORDS 2048u /* 8 banks x 256 */
#define AGC_FIXED_WORDS    36864u /* 36 banks x 1024 — what a flight rope holds */

/* Superbank addressing can name words past the populated 36 K (FEXT selects a
 * 4 K window over banks 30-37 octal).  The array covers the whole addressable
 * span so an out-of-range fetch reads unwritten zeroes and fails parity — which
 * is what the hardware does when the sense lines find no rope. */
#define AGC_FIXED_ADDR_SPAN 40960u

typedef struct agc_memory {
    agc_word erasable[AGC_ERASABLE_WORDS];
    agc_word fixed[AGC_FIXED_ADDR_SPAN]; /* raw rope layout, parity in bit 15 */
} agc_memory;

/* Erasable addresses 0-7 are the central registers, not core; the CPU reaches
 * them through RSC/WSC and never through these functions.  Address 7 is the
 * hard-wired zero register. */
#define AGC_ERASABLE_ZERO_REG 7u

void agc_memory_clear(agc_memory *m);

/* Destructive read: returns the word with its sign duplicated into bit 16 and
 * leaves +0 behind, as the core sense/inhibit cycle does. */
agc_word agc_memory_read_erasable(agc_memory *m, agc_word addr);
void     agc_memory_write_erasable(agc_memory *m, agc_word addr, agc_word data);

/* Fixed reads are non-destructive.  `agc_memory_read_fixed` returns the 15-bit
 * rope word with its sign duplicated into bit 16; `..._raw` returns the stored
 * word including its parity bit, for the parity check. */
agc_word agc_memory_read_fixed(const agc_memory *m, unsigned addr);
agc_word agc_memory_read_fixed_raw(const agc_memory *m, unsigned addr);
void     agc_memory_write_fixed_raw(agc_memory *m, unsigned addr, agc_word raw);

/* True when the stored rope word has correct (odd) parity. */
bool agc_memory_fixed_parity_ok(const agc_memory *m, unsigned addr);

/* Load a `yaYUL --hardware` rope image.  Returns the number of words read, or
 * -1 on I/O error.  Words beyond the file are left as they were. */
long agc_memory_load_rope(agc_memory *m, const char *path);

/* Load a rope-module dump (a bank pair rather than a whole rope) at a bank
 * offset. `bank` is the fixed-memory bank number the dump's first word belongs
 * to. Returns the number of words read, or -1 on I/O error. */
long agc_memory_load_rope_at(agc_memory *m, const char *path, unsigned bank);

/* Address arithmetic, shared with the CPU's S-register handling. */
agc_word agc_erasable_absolute(agc_word s, agc_word eb);
unsigned agc_fixed_absolute(agc_word s, agc_word fb, agc_word fext);

#endif /* AGC_MEMORY_H */
