#include "memory.h"

#include <stdio.h>
#include <string.h>

/* Banked erasable lives at S = 1400-1777 and is selected by EB; everything
 * below that is unswitched. */
#define ERASABLE_BANKED_LO AGC_BITS(9, 10) /* 001400 */
#define ERASABLE_BANKED_HI 01777u

/* Banked fixed lives at S = 2000-3777 and is selected by FB (+ FEXT for the
 * superbanks); S = 4000-7777 is the unswitched fixed-fixed banks 2 and 3. */
#define FIXED_BANKED_LO 02000u
#define FIXED_BANKED_HI 03777u

void agc_memory_clear(agc_memory *m)
{
    memset(m->erasable, 0, sizeof m->erasable);
    memset(m->fixed, 0, sizeof m->fixed);
}

agc_word agc_erasable_absolute(agc_word s, agc_word eb)
{
    if (s >= ERASABLE_BANKED_LO && s <= ERASABLE_BANKED_HI) {
        /* EB holds the bank in bits 9-11, already aligned to the 256-word
         * stride, so it ORs straight on top of the in-bank offset. */
        return agc_w((s & 0377u) | eb);
    }
    return s;
}

unsigned agc_fixed_absolute(agc_word s, agc_word fb, agc_word fext)
{
    if (s < FIXED_BANKED_LO || s > FIXED_BANKED_HI) {
        /* Fixed-fixed: S is already the absolute address. */
        return s;
    }

    unsigned offset = s & 01777u;

    /* Superbanking is in force only when FEXT selects bank set 4 or above and
     * FB is already in the top eight banks; otherwise FEXT is ignored entirely.
     * (Information Series #10; FEXT is channel 7.) */
    if ((unsigned)(fext >> 4) >= 4u && fb >= 060000u) {
        return offset | (unsigned)(fb & 0016000u) | ((unsigned)fext << 9);
    }
    return offset | fb;
}

agc_word agc_memory_read_erasable(agc_memory *m, agc_word addr)
{
    if (addr == AGC_ERASABLE_ZERO_REG) {
        return 0; /* hard-wired to +0 */
    }
    if (addr >= AGC_ERASABLE_WORDS) {
        return 0;
    }

    /* Erasable words are 15 bits; the sign is duplicated into bit 16 on the way
     * out so the adder and the overflow tests see a 16-bit signed quantity. */
    agc_word v = agc_w(m->erasable[addr] & (unsigned)~AGC_BIT(16));
    v = agc_w(v | (unsigned)((v & AGC_BIT(15)) << 1));

    /* Coincident-current core reads by driving the cell to +0 and sensing the
     * flux change: the read destroys the contents. The rewrite happens later in
     * the memory cycle (cpu.c, before T10). */
    m->erasable[addr] = 0;
    return v;
}

void agc_memory_write_erasable(agc_memory *m, agc_word addr, agc_word data)
{
    if (addr == AGC_ERASABLE_ZERO_REG || addr >= AGC_ERASABLE_WORDS) {
        return; /* writes to the zero register are swallowed */
    }

    /* An erasable word is fifteen bits and a parity bit; there is no bit 16 in
     * the core, so bit 16 is simply dropped here. Deciding *which* of bits 15
     * and 16 survives when they disagree is not memory's business — it is the
     * G-write gate's, and it depends on what the machine is doing (see
     * agc_cpu_write_g in cpu.c). */
    m->erasable[addr] = agc_w(data & AGC_BITS(1, 15));
}

agc_word agc_memory_read_fixed_raw(const agc_memory *m, unsigned addr)
{
    return addr < AGC_FIXED_ADDR_SPAN ? m->fixed[addr] : 0;
}

agc_word agc_memory_read_fixed(const agc_memory *m, unsigned addr)
{
    agc_word raw = agc_memory_read_fixed_raw(m, addr);
    /* Drop the parity bit sitting in position 15 and put the word's own sign
     * (stored in position 16) back into both 15 and 16. */
    unsigned v = raw & (unsigned)~AGC_BIT(15);
    v |= (v & AGC_BIT(16)) >> 1;
    return agc_w(v);
}

void agc_memory_write_fixed_raw(agc_memory *m, unsigned addr, agc_word raw)
{
    if (addr < AGC_FIXED_ADDR_SPAN) {
        m->fixed[addr] = raw;
    }
}

bool agc_memory_fixed_parity_ok(const agc_memory *m, unsigned addr)
{
    /* Rope words carry odd parity across all 16 stored bits. An all-zero word
     * (unwoven rope, or an address past the populated 36 K) has even parity and
     * so fails — which is exactly the hardware's behaviour and the reason the
     * PARITY FAIL alarm exists. */
    unsigned v = agc_memory_read_fixed_raw(m, addr);
    unsigned bits = 0;
    while (v) {
        bits ^= v & 1u;
        v >>= 1;
    }
    return bits == 1u;
}

static long load_words(agc_memory *m, const char *path, unsigned first_word)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }

    long count = 0;
    unsigned char pair[2];
    unsigned addr = first_word;
    while (addr < AGC_FIXED_ADDR_SPAN && fread(pair, 1, 2, f) == 2) {
        /* Rope images are big-endian, one 16-bit word per address. */
        m->fixed[addr++] = agc_w(((unsigned)pair[0] << 8) | pair[1]);
        count++;
    }

    bool bad = ferror(f) != 0;
    fclose(f);
    return bad ? -1 : count;
}

long agc_memory_load_rope(agc_memory *m, const char *path)
{
    return load_words(m, path, 0);
}

long agc_memory_load_rope_at(agc_memory *m, const char *path, unsigned bank)
{
    return load_words(m, path, bank * 1024u);
}
