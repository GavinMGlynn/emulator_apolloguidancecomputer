#include "dsky.h"

#include <stdio.h>
#include <string.h>

#include "../agc.h"

/* The digit-to-relay-code table, transcribed from `RELTAB` in Luminary 099's
 * FIXED-FIXED_CONSTANT_POOL: the low five bits of each entry are the relay code
 * and the position in the table is the digit. Confirmed independently against
 * the seven-segment artwork in ext/virtualagc's yaDSKY, which is named by code.
 *
 *   0 -> 025    1 -> 003    2 -> 031    3 -> 033    4 -> 017
 *   5 -> 036    6 -> 034    7 -> 023    8 -> 035    9 -> 037    blank -> 000
 *
 * Anything else is a code the relays can hold but no digit displays; the real
 * panel shows some meaningless set of segments and we show a blank. */
static uint8_t decode_digit(unsigned code)
{
    switch (code & 037u) {
    case 025: return 0;
    case 003: return 1;
    case 031: return 2;
    case 033: return 3;
    case 017: return 4;
    case 036: return 5;
    case 034: return 6;
    case 023: return 7;
    case 035: return 8;
    case 037: return 9;
    /* 000 is the blank code the software writes deliberately. The other 21
     * codes are holdable but meaningless — the real panel lights whatever
     * segments they happen to drive — and we show them as blank rather than
     * inventing a digit. */
    default: return AGC_DSKY_BLANK;
    }
}

/* Which display positions each relay bank drives, and which sign relay (if
 * any) rides in bit 11. Information Series #30 paragraph 30-145A gives the
 * shape — banks 00-10 octal are the three five-digit registers, 11-13 are
 * NOUN, VERB and PROGRAM — and the position-by-position assignment is the one
 * every DSKY implementation shares, checked here against a booted rope.
 *
 * `left` takes bits 10-6 and `right` bits 5-1. Bank 8 (octal 10) carries only a
 * right digit: R1 has five digits and they do not divide evenly into pairs. */
#define NO_DIGIT AGC_DSKY_DIGIT_COUNT
#define NO_SIGN  AGC_DSKY_REGISTERS

static const struct {
    uint8_t left, right;
    uint8_t sign_register;
    bool sign_is_plus;
} bank_layout[AGC_DSKY_BANKS] = {
    [001] = { AGC_DSKY_R3D4,   AGC_DSKY_R3D5,  AGC_DSKY_R3, false },
    [002] = { AGC_DSKY_R3D2,   AGC_DSKY_R3D3,  AGC_DSKY_R3, true  },
    [003] = { AGC_DSKY_R2D5,   AGC_DSKY_R3D1,  NO_SIGN,     false },
    [004] = { AGC_DSKY_R2D3,   AGC_DSKY_R2D4,  AGC_DSKY_R2, false },
    [005] = { AGC_DSKY_R2D1,   AGC_DSKY_R2D2,  AGC_DSKY_R2, true  },
    [006] = { AGC_DSKY_R1D4,   AGC_DSKY_R1D5,  AGC_DSKY_R1, false },
    [007] = { AGC_DSKY_R1D2,   AGC_DSKY_R1D3,  AGC_DSKY_R1, true  },
    [010] = { NO_DIGIT,        AGC_DSKY_R1D1,  NO_SIGN,     false },
    [011] = { AGC_DSKY_NOUN1,  AGC_DSKY_NOUN2, NO_SIGN,     false },
    [012] = { AGC_DSKY_VERB1,  AGC_DSKY_VERB2, NO_SIGN,     false },
    [013] = { AGC_DSKY_PROG1,  AGC_DSKY_PROG2, NO_SIGN,     false },
    [014] = { NO_DIGIT,        NO_DIGIT,       NO_SIGN,     false },
    [000] = { NO_DIGIT,        NO_DIGIT,       NO_SIGN,     false },
};

/* Long enough that the interrupt is taken and the code read no matter how busy
 * the machine is — the flight software's own keyboard scan runs at 200 Hz, so
 * a tenth of a second is generous without being unrealistic for a finger. */
#define AGC_DSKY_DEFAULT_HOLD (AGC_TIMEPULSE_CLOCK_HZ / 10u)

void agc_dsky_reset(agc_dsky *d)
{
    memset(d, 0, sizeof *d);
    for (unsigned i = 0; i < AGC_DSKY_DIGIT_COUNT; ++i) {
        d->digit[i] = AGC_DSKY_BLANK;
    }
}

static void latch_relay_word(agc_dsky *d, agc_word value)
{
    unsigned bank = ((unsigned)value >> 11) & 017u;
    agc_word relays = agc_w(value & 03777u);

    /* The bank field is four bits but only thirteen banks are wired (octal 00
     * through 14, Information Series #30 paragraph 30-145A), so codes 15-17
     * address no relays at all. The flight software does write them — the
     * Validation rope walks the whole field — and on the real panel nothing
     * happens. */
    if (bank >= AGC_DSKY_BANKS) {
        return;
    }

    d->relay[bank] = relays;
    d->relay_writes++;

    if (bank == 014u) {
        d->status = relays;
        return;
    }

    const unsigned left_code = ((unsigned)relays >> 5) & 037u;
    const unsigned right_code = (unsigned)relays & 037u;
    const bool sign_relay = (relays & AGC_BIT(11)) != 0;

    if (bank_layout[bank].left != NO_DIGIT) {
        d->digit[bank_layout[bank].left] = decode_digit(left_code);
    }
    if (bank_layout[bank].right != NO_DIGIT) {
        d->digit[bank_layout[bank].right] = decode_digit(right_code);
    }

    if (bank_layout[bank].sign_register != NO_SIGN) {
        const unsigned reg = bank_layout[bank].sign_register;
        if (bank_layout[bank].sign_is_plus) {
            d->sign_plus[reg] = sign_relay;
        } else {
            d->sign_minus[reg] = sign_relay;
        }
    }
}

void agc_dsky_channel_write(agc *m, unsigned channel, agc_word value)
{
    agc_dsky *d = &m->dsky;
    switch (channel) {
    case AGC_CH_DSKY:
        latch_relay_word(d, value);
        break;
    case AGC_CH_LAMPS:
        d->lamps = value;
        break;
    default:
        break;
    }
}

void agc_dsky_tick(agc *m)
{
    agc_dsky *d = &m->dsky;

    /* The flash is not a software function: module A24 forms it from scaler
     * stages 16 and 17 and it runs whether or not any program is using it. */
    d->flash = m->scaler.flash_on;

    if (d->hold_main != 0 && --d->hold_main == 0) {
        d->keys_main = 0;
        agc_channel_write(&m->channels, AGC_CH_DSKY_IN, 0);
    }
    if (d->hold_nav != 0 && --d->hold_nav == 0) {
        d->keys_nav = 0;
        agc_channel_write(&m->channels, AGC_CH_DSKY_IN2, 0);
    }
}

void agc_dsky_press(agc *m, enum agc_dsky_key key, uint32_t hold)
{
    agc_dsky *d = &m->dsky;
    d->keys_main = agc_w((unsigned)key & 037u);
    d->hold_main = hold ? hold : AGC_DSKY_DEFAULT_HOLD;
    agc_channel_write(&m->channels, AGC_CH_DSKY_IN, d->keys_main);
    m->cpu.interrupts[AGC_RUPT_KEYRUPT1] = true;
}

void agc_dsky_press_nav(agc *m, agc_word bits, uint32_t hold)
{
    agc_dsky *d = &m->dsky;
    d->keys_nav = agc_w(bits & 0177u);
    d->hold_nav = hold ? hold : AGC_DSKY_DEFAULT_HOLD;
    agc_channel_write(&m->channels, AGC_CH_DSKY_IN2, d->keys_nav);
    m->cpu.interrupts[AGC_RUPT_KEYRUPT2] = true;
}

void agc_dsky_release(agc *m)
{
    agc_dsky *d = &m->dsky;
    d->keys_main = 0;
    d->keys_nav = 0;
    d->hold_main = 0;
    d->hold_nav = 0;
    agc_channel_write(&m->channels, AGC_CH_DSKY_IN, 0);
    agc_channel_write(&m->channels, AGC_CH_DSKY_IN2, 0);
}

uint8_t agc_dsky_digit(const agc_dsky *d, enum agc_dsky_digit position)
{
    return d->digit[position];
}

int agc_dsky_sign(const agc_dsky *d, enum agc_dsky_register reg)
{
    if (d->sign_plus[reg]) {
        return 1;
    }
    return d->sign_minus[reg] ? -1 : 0;
}

bool agc_dsky_lamp(const agc_dsky *d, agc_word lamp)
{
    if ((d->lamps & lamp) == 0) {
        return false;
    }
    /* KEY REL and OPR ERR are flashed by the DSKY, not by the program: module
     * A16 gate U16047 gates both with FLASH, and the VERB/NOUN flash with its
     * complement — so they blink in antiphase with the displays. */
    if (lamp == AGC_DSKY_LAMP_KEY_REL || lamp == AGC_DSKY_LAMP_OPR_ERR) {
        return !d->flash;
    }
    return true;
}

bool agc_dsky_verb_noun_visible(const agc_dsky *d)
{
    if ((d->lamps & AGC_DSKY_FLASH_ENABLE) == 0) {
        return true;
    }
    return d->flash;
}

int agc_dsky_format(const agc_dsky *d, char *buf, size_t len)
{
    char text[AGC_DSKY_DIGIT_COUNT];
    for (unsigned i = 0; i < AGC_DSKY_DIGIT_COUNT; ++i) {
        uint8_t v = d->digit[i];
        text[i] = (v == AGC_DSKY_BLANK) ? ' ' : (char)('0' + v);
    }
    static const char sign_char[3] = { '-', ' ', '+' };

    /* What the relays are holding, never what the flash happens to be doing to
     * it this instant: a display that blinked in and out of a golden dump would
     * make the dump a function of when it was taken. The flash is reported as a
     * flag instead, and a frontend that wants to blink asks
     * agc_dsky_verb_noun_visible(). */
    return snprintf(
        buf, len,
        "PROG %c%c VERB %c%c NOUN %c%c "
        "R1 %c%c%c%c%c%c R2 %c%c%c%c%c%c R3 %c%c%c%c%c%c%s",
        text[AGC_DSKY_PROG1], text[AGC_DSKY_PROG2],
        text[AGC_DSKY_VERB1], text[AGC_DSKY_VERB2],
        text[AGC_DSKY_NOUN1], text[AGC_DSKY_NOUN2],
        sign_char[agc_dsky_sign(d, AGC_DSKY_R1) + 1],
        text[AGC_DSKY_R1D1], text[AGC_DSKY_R1D2], text[AGC_DSKY_R1D3],
        text[AGC_DSKY_R1D4], text[AGC_DSKY_R1D5],
        sign_char[agc_dsky_sign(d, AGC_DSKY_R2) + 1],
        text[AGC_DSKY_R2D1], text[AGC_DSKY_R2D2], text[AGC_DSKY_R2D3],
        text[AGC_DSKY_R2D4], text[AGC_DSKY_R2D5],
        sign_char[agc_dsky_sign(d, AGC_DSKY_R3) + 1],
        text[AGC_DSKY_R3D1], text[AGC_DSKY_R3D2], text[AGC_DSKY_R3D3],
        text[AGC_DSKY_R3D4], text[AGC_DSKY_R3D5],
        (d->lamps & AGC_DSKY_FLASH_ENABLE) ? " FLASH" : "");
}
