/* The DSKY: display and keyboard.
 *
 * The AGC does not drive a display. It drives *relays* — thirteen banks of up
 * to eleven latching relays in each DSKY, set and reset a bank at a time by
 * "relay words" written to output channel 10. The digits are whatever the
 * relays are currently holding, which is why the display keeps showing the last
 * thing written even while the program is off doing something else, and why a
 * program that stops half way through updating leaves half a number on the
 * panel. We model the relays and derive the digits, rather than the reverse.
 *
 * References, in the order they were trusted:
 *
 *   - **Information Series #30, the Block II AGC**, table 30-5 and paragraphs
 *     30-77 / 30-145A-C: the channel assignments, the relay word format, and
 *     what each bank drives. This is the primary source and the table was read
 *     off the rendered page, not an OCR extraction.
 *   - **MIT's own flight software** for the two encodings the hardware
 *     documents do not tabulate: `RELTAB` in Luminary 099's fixed-fixed
 *     constant pool gives the digit-to-relay-code table, and the `CHARIN`
 *     dispatch in PINBALL gives the keyboard codes, each one commented with the
 *     key it belongs to.
 *   - **The gates** (`ext/agc_simulation`) for the flash, which is not a
 *     software function at all: module A24 gate U24025 forms FLASH = NOR(FS17,
 *     FS16) straight off the scaler, and module A16 gate U16047 uses it to
 *     blink the VERB/NOUN displays in antiphase with the KEY REL and OPR ERR
 *     lamps.
 *
 * Channel 10 is write-only from the program's side and the relay state is not
 * readable, so nothing here feeds back into the machine. The keyboard does:
 * a keypress puts a five-bit code in channel 15 (or 16) and requests KEYRUPT.
 */
#ifndef AGC_DSKY_H
#define AGC_DSKY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../agc_word.h"

struct agc;

/* Thirteen relay banks, octal 00 through 14 (Information Series #30, 30-145A).
 * Bank 00 is addressable and unused; the flight software never writes it. */
#define AGC_DSKY_BANKS 015u

/* Five digits each in R1, R2 and R3, then the two-digit PROG, VERB and NOUN
 * displays. Indices into `agc_dsky.digit`. */
enum agc_dsky_digit {
    AGC_DSKY_R1D1, AGC_DSKY_R1D2, AGC_DSKY_R1D3, AGC_DSKY_R1D4, AGC_DSKY_R1D5,
    AGC_DSKY_R2D1, AGC_DSKY_R2D2, AGC_DSKY_R2D3, AGC_DSKY_R2D4, AGC_DSKY_R2D5,
    AGC_DSKY_R3D1, AGC_DSKY_R3D2, AGC_DSKY_R3D3, AGC_DSKY_R3D4, AGC_DSKY_R3D5,
    AGC_DSKY_PROG1, AGC_DSKY_PROG2,
    AGC_DSKY_VERB1, AGC_DSKY_VERB2,
    AGC_DSKY_NOUN1, AGC_DSKY_NOUN2,
    AGC_DSKY_DIGIT_COUNT
};

/* A digit position showing nothing. The blank relay code is 0, and so is the
 * power-on state of every relay, so a machine that has never written the
 * display reads out blank rather than "00000". */
#define AGC_DSKY_BLANK 0xFFu

/* The three data registers, for the sign displays. */
enum agc_dsky_register { AGC_DSKY_R1, AGC_DSKY_R2, AGC_DSKY_R3, AGC_DSKY_REGISTERS };

/* Channel 11 lamp bits (Information Series #30, table 30-5). Bits 8-15 are
 * spare, except bit 10, which also drives the restart flip-flop. */
#define AGC_DSKY_LAMP_ISS_WARNING AGC_BIT(1)
#define AGC_DSKY_LAMP_COMP_ACTY   AGC_BIT(2)
#define AGC_DSKY_LAMP_UPLINK_ACTY AGC_BIT(3)
#define AGC_DSKY_LAMP_TEMP        AGC_BIT(4)
#define AGC_DSKY_LAMP_KEY_REL     AGC_BIT(5)
#define AGC_DSKY_FLASH_ENABLE     AGC_BIT(6)
#define AGC_DSKY_LAMP_OPR_ERR     AGC_BIT(7)
#define AGC_DSKY_CH11_RESTART     AGC_BIT(10)

/* Relay bank 14 (octal): the status and caution relays (30-145B). Bit 5 is a
 * spare indicator and bit 7 enables the PIPAs rather than lighting anything. */
#define AGC_DSKY_STATUS_AUTO        AGC_BIT(1)
#define AGC_DSKY_STATUS_HOLD        AGC_BIT(2)
#define AGC_DSKY_STATUS_FREE        AGC_BIT(3)
#define AGC_DSKY_STATUS_NO_ATT      AGC_BIT(4)
#define AGC_DSKY_STATUS_SPARE       AGC_BIT(5)
#define AGC_DSKY_STATUS_GIMBAL_LOCK AGC_BIT(6)
#define AGC_DSKY_STATUS_PIPA_ENABLE AGC_BIT(7)
#define AGC_DSKY_STATUS_TRACKER     AGC_BIT(8)
#define AGC_DSKY_STATUS_PROG        AGC_BIT(9)

/* Keyboard codes, read straight off the `CHARIN` dispatch table in PINBALL,
 * where MIT commented every entry with the key it decodes. Codes not listed
 * here reach `CHARALRM` — the software's own "that is not a key" path. */
enum agc_dsky_key {
    AGC_KEY_0     = 020,
    AGC_KEY_1     = 001,
    AGC_KEY_2     = 002,
    AGC_KEY_3     = 003,
    AGC_KEY_4     = 004,
    AGC_KEY_5     = 005,
    AGC_KEY_6     = 006,
    AGC_KEY_7     = 007,
    AGC_KEY_8     = 010,
    AGC_KEY_9     = 011,
    AGC_KEY_VERB  = 021,
    AGC_KEY_RSET  = 022, /* "error light reset" in the listing */
    AGC_KEY_KEYREL = 031,
    AGC_KEY_PLUS  = 032,
    AGC_KEY_MINUS = 033,
    AGC_KEY_ENTR  = 034,
    AGC_KEY_CLR   = 036,
    AGC_KEY_NOUN  = 037,
};

/* Channel 16 discretes, which are not keyboard codes: the optics MARK and MARK
 * REJECT buttons sit in bits 6 and 7 and raise the same interrupt. */
#define AGC_DSKY_MARK        AGC_BIT(6)
#define AGC_DSKY_MARK_REJECT AGC_BIT(7)

typedef struct agc_dsky {
    /* What the relays are holding. Bits 11-1 of the last relay word written to
     * each bank; the display is a function of this and nothing else. */
    agc_word relay[AGC_DSKY_BANKS];

    /* The same thing decoded, maintained on every channel-10 write so a
     * frontend never has to know the relay layout. */
    uint8_t digit[AGC_DSKY_DIGIT_COUNT];
    bool sign_plus[AGC_DSKY_REGISTERS];
    bool sign_minus[AGC_DSKY_REGISTERS];

    /* Channel 11 as last written, and bank 14 as last latched. */
    agc_word lamps;
    agc_word status;

    /* The flash phase, copied from the scaler each timing pulse. */
    bool flash;

    /* Keyboard. A key is held down for a while and then released, exactly as a
     * finger does it; `hold` counts the timing pulses left. */
    agc_word keys_main;
    agc_word keys_nav;
    uint32_t hold_main;
    uint32_t hold_nav;

    /* Every relay word the program has written, counted. A rope that is not
     * driving the display at all is a different failure from one that is
     * driving it with the wrong values. */
    uint64_t relay_writes;
} agc_dsky;

void agc_dsky_reset(agc_dsky *d);

/* Called by the CPU on every channel write; ignores channels it does not own. */
void agc_dsky_channel_write(struct agc *m, unsigned channel, agc_word value);

/* Called once per timing pulse: samples the flash phase and times key holds. */
void agc_dsky_tick(struct agc *m);

/* Press a key on the main panel: five-bit code into channel 15, KEYRUPT1
 * requested. Released automatically after `hold` timing pulses — pass 0 for the
 * default, which is long enough for the interrupt to be taken and the code read
 * however busy the machine is. */
void agc_dsky_press(struct agc *m, enum agc_dsky_key key, uint32_t hold);

/* The navigation panel's MARK and MARK REJECT buttons: channel 16, KEYRUPT2. */
void agc_dsky_press_nav(struct agc *m, agc_word bits, uint32_t hold);

/* Release whatever is held, now. */
void agc_dsky_release(struct agc *m);

/* What a digit position is showing: 0-9, or AGC_DSKY_BLANK. */
uint8_t agc_dsky_digit(const agc_dsky *d, enum agc_dsky_digit position);

/* A register's sign: +1, -1, or 0 for blank. Both relays set reads as "+" —
 * the sign is three segments and the plus relay lights all of them. */
int agc_dsky_sign(const agc_dsky *d, enum agc_dsky_register reg);

/* Is a lamp lit *now*? KEY REL and OPR ERR are flashed by the DSKY itself, so
 * this is not simply a channel 11 bit. */
bool agc_dsky_lamp(const agc_dsky *d, agc_word lamp);

/* Are the VERB and NOUN displays currently visible? They blink when the
 * program sets the flash bit. */
bool agc_dsky_verb_noun_visible(const agc_dsky *d);

/* The whole display as text, for tracing and for golden dumps:
 * "PROG 06 VERB 37 NOUN 00 R1 +00000 R2 ..." Blank positions print as spaces,
 * so a half-updated display looks half-updated. */
int agc_dsky_format(const agc_dsky *d, char *buf, size_t len);

#endif /* AGC_DSKY_H */
