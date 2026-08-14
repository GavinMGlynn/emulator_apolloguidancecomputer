/* An interactive DSKY.
 *
 * The core is unchanged and unaware: this frontend calls agc_tick() like the
 * headless one does, reads `agc_dsky` for what the relays are holding, and
 * draws it. Nothing here feeds back except keystrokes, which go in through the
 * same agc_dsky_press() a scripted run uses.
 *
 * The seven-segment digits are drawn as segments rather than glyphs, because
 * that is what the panel is — and because it means no font, no image assets and
 * nothing to install. A blank position draws nothing at all, which matters:
 * a half-updated display is a real thing to see (see dsky.h).
 *
 * Pacing. The guide's advice is to pace from the audio queue rather than a
 * wall clock, and the AGC has no audio, so this paces on emulated time instead:
 * each frame runs however many MCTs a frame of real time is worth, and if the
 * host cannot keep up it runs fewer rather than sliding. Determinism lives in
 * the headless frontend, which is what the goldens describe; this one is for
 * looking at.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#ifdef AGC_HAVE_PNG
#include <png.h>
#endif

#include "agc.h"
#include "dsky/dsky.h"

/* Emulated microseconds per MCT, near enough for pacing: 11.71875 us. */
#define MCTS_PER_SECOND 85333u

/* Panel geometry. Everything scales off the digit size. */
#define SEG_W 8
#define SEG_L 34
#define DIGIT_W (SEG_L + 2 * SEG_W)
#define DIGIT_H (2 * SEG_L + 3 * SEG_W)
#define DIGIT_GAP 12
#define MARGIN 28

static const SDL_Color GREEN = { 60, 255, 90, 255 };
static const SDL_Color DIM = { 24, 44, 28, 255 };
static const SDL_Color PANEL = { 26, 26, 28, 255 };
static const SDL_Color LAMP_ON = { 250, 240, 190, 255 };
static const SDL_Color LAMP_OFF = { 52, 50, 44, 255 };
static const SDL_Color LABEL = { 200, 200, 200, 255 };

/* Which of the seven segments each digit lights, in the order
 * a (top), b (upper right), c (lower right), d (bottom), e (lower left),
 * f (upper left), g (middle). */
static const uint8_t SEGMENTS[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};

static void fill(SDL_Renderer *r, SDL_Color c, float x, float y, float w, float h)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect rect = { x, y, w, h };
    SDL_RenderFillRect(r, &rect);
}

/* One seven-segment digit. `value` is 0-9, or AGC_DSKY_BLANK for a position the
 * relays are holding blank. */
static void draw_digit(SDL_Renderer *r, float x, float y, uint8_t value)
{
    const uint8_t on = (value == AGC_DSKY_BLANK) ? 0u : SEGMENTS[value];
    const float w = SEG_W, l = SEG_L;

    struct { float x, y, w, h; } seg[7] = {
        { x + w,     y,             l, w },  /* a */
        { x + w + l, y + w,         w, l },  /* b */
        { x + w + l, y + 2 * w + l, w, l },  /* c */
        { x + w,     y + 2 * (w + l), l, w }, /* d */
        { x,         y + 2 * w + l, w, l },  /* e */
        { x,         y + w,         w, l },  /* f */
        { x + w,     y + w + l,     l, w },  /* g */
    };
    for (int i = 0; i < 7; ++i) {
        fill(r, (on & (1u << i)) ? GREEN : DIM, seg[i].x, seg[i].y, seg[i].w, seg[i].h);
    }
}

/* The three-segment sign: a horizontal bar, plus a vertical one for plus. */
static void draw_sign(SDL_Renderer *r, float x, float y, int sign)
{
    const float w = SEG_W, l = SEG_L;
    fill(r, sign != 0 ? GREEN : DIM, x, y + w + l, l, w);
    fill(r, sign > 0 ? GREEN : DIM, x + l / 2 - w / 2, y + l / 2 + w, w, l);
}

/* A tiny 5x7 block font, enough for the panel's fixed labels. */
static const char *GLYPH_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789";
static const uint8_t GLYPHS[][5] = {
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x00,0x00,0x00,0x00},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
    {0x06,0x49,0x49,0x29,0x1E},
};

static void draw_text(SDL_Renderer *r, SDL_Color c, float x, float y, float px,
                      const char *text)
{
    for (const char *p = text; *p; ++p) {
        const char *at = strchr(GLYPH_CHARS, *p);
        if (at) {
            const uint8_t *g = GLYPHS[at - GLYPH_CHARS];
            for (int col = 0; col < 5; ++col) {
                for (int row = 0; row < 7; ++row) {
                    if (g[col] & (1u << row)) {
                        fill(r, c, x + (float)col * px, y + (float)row * px, px, px);
                    }
                }
            }
        }
        x += 6 * px;
    }
}

static void draw_pair(SDL_Renderer *r, const agc_dsky *d, float x, float y,
                      enum agc_dsky_digit a, enum agc_dsky_digit b, bool visible)
{
    draw_digit(r, x, y, visible ? agc_dsky_digit(d, a) : AGC_DSKY_BLANK);
    draw_digit(r, x + DIGIT_W + DIGIT_GAP, y, visible ? agc_dsky_digit(d, b) : AGC_DSKY_BLANK);
}

static void draw_register(SDL_Renderer *r, const agc_dsky *d, float x, float y,
                          enum agc_dsky_register reg, enum agc_dsky_digit first)
{
    draw_sign(r, x, y, agc_dsky_sign(d, reg));
    for (int i = 0; i < 5; ++i) {
        draw_digit(r, x + (float)(i + 1) * (DIGIT_W + DIGIT_GAP), y,
                   agc_dsky_digit(d, (enum agc_dsky_digit)((unsigned)first + (unsigned)i)));
    }
}

struct lamp { const char *label; agc_word bit; };

static const struct lamp LAMPS[] = {
    { "COMP ACTY", AGC_DSKY_LAMP_COMP_ACTY },
    { "UPLINK", AGC_DSKY_LAMP_UPLINK_ACTY },
    { "TEMP", AGC_DSKY_LAMP_TEMP },
    { "KEY REL", AGC_DSKY_LAMP_KEY_REL },
    { "OPR ERR", AGC_DSKY_LAMP_OPR_ERR },
    { "ISS WARN", AGC_DSKY_LAMP_ISS_WARNING },
};

static void draw_panel(SDL_Renderer *r, const agc_dsky *d)
{
    SDL_SetRenderDrawColor(r, PANEL.r, PANEL.g, PANEL.b, PANEL.a);
    SDL_RenderClear(r);

    const float lamp_w = 124, lamp_h = 34;
    for (size_t i = 0; i < sizeof LAMPS / sizeof *LAMPS; ++i) {
        const float lx = MARGIN + (float)(i % 2) * (lamp_w + 10);
        const float ly = MARGIN + (float)(i / 2) * (lamp_h + 8);
        fill(r, agc_dsky_lamp(d, LAMPS[i].bit) ? LAMP_ON : LAMP_OFF,
             lx, ly, lamp_w, lamp_h);
        draw_text(r, PANEL, lx + 8, ly + 12, 2.0f, LAMPS[i].label);
    }

    const float right = MARGIN + 2 * lamp_w + 40;
    const bool vn = agc_dsky_verb_noun_visible(d);
    float y = MARGIN;

    draw_text(r, LABEL, right, y + 12, 2.0f, "PROG");
    draw_pair(r, d, right + 110, y, AGC_DSKY_PROG1, AGC_DSKY_PROG2, true);
    y += DIGIT_H + 16;
    draw_text(r, LABEL, right, y + 12, 2.0f, "VERB");
    draw_pair(r, d, right + 110, y, AGC_DSKY_VERB1, AGC_DSKY_VERB2, vn);
    draw_text(r, LABEL, right + 250, y + 12, 2.0f, "NOUN");
    draw_pair(r, d, right + 340, y, AGC_DSKY_NOUN1, AGC_DSKY_NOUN2, vn);

    y += DIGIT_H + 24;
    const float rx = MARGIN;
    draw_register(r, d, rx, y, AGC_DSKY_R1, AGC_DSKY_R1D1);
    y += DIGIT_H + 14;
    draw_register(r, d, rx, y, AGC_DSKY_R2, AGC_DSKY_R2D1);
    y += DIGIT_H + 14;
    draw_register(r, d, rx, y, AGC_DSKY_R3, AGC_DSKY_R3D1);

    SDL_RenderPresent(r);
}

#ifdef AGC_HAVE_PNG
/* Write what is actually on the panel to a PNG. The guide's rule is to verify
 * on the real output rather than a proxy, and for a display that means looking
 * at it — including in a headless run, where this is the only way to. */
static bool screenshot(SDL_Renderer *r, const char *path)
{
    SDL_Surface *s = SDL_RenderReadPixels(r, NULL);
    if (!s) {
        fprintf(stderr, "SDL_RenderReadPixels: %s\n", SDL_GetError());
        return false;
    }
    SDL_Surface *rgb = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!rgb) {
        return false;
    }

    bool ok = false;
    FILE *f = fopen(path, "wb");
    png_structp png = NULL;
    png_infop info = NULL;
    if (f) {
        png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    }
    if (png) {
        info = png_create_info_struct(png);
    }
    if (info && !setjmp(png_jmpbuf(png))) {
        png_init_io(png, f);
        png_set_IHDR(png, info, (png_uint_32)rgb->w, (png_uint_32)rgb->h, 8,
                     PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        png_write_info(png, info);
        for (int y = 0; y < rgb->h; ++y) {
            png_write_row(png, (png_bytep)rgb->pixels + (size_t)y * (size_t)rgb->pitch);
        }
        png_write_end(png, NULL);
        ok = true;
    }
    if (png) {
        png_destroy_write_struct(&png, info ? &info : NULL);
    }
    if (f) {
        fclose(f);
    }
    SDL_DestroySurface(rgb);
    return ok;
}
#endif

static bool key_for(SDL_Keycode k, enum agc_dsky_key *out)
{
    switch (k) {
    case SDLK_0: *out = AGC_KEY_0; return true;
    case SDLK_1: *out = AGC_KEY_1; return true;
    case SDLK_2: *out = AGC_KEY_2; return true;
    case SDLK_3: *out = AGC_KEY_3; return true;
    case SDLK_4: *out = AGC_KEY_4; return true;
    case SDLK_5: *out = AGC_KEY_5; return true;
    case SDLK_6: *out = AGC_KEY_6; return true;
    case SDLK_7: *out = AGC_KEY_7; return true;
    case SDLK_8: *out = AGC_KEY_8; return true;
    case SDLK_9: *out = AGC_KEY_9; return true;
    case SDLK_V: *out = AGC_KEY_VERB; return true;
    case SDLK_N: *out = AGC_KEY_NOUN; return true;
    case SDLK_RETURN: *out = AGC_KEY_ENTR; return true;
    case SDLK_R: *out = AGC_KEY_RSET; return true;
    case SDLK_C: *out = AGC_KEY_CLR; return true;
    case SDLK_K: *out = AGC_KEY_KEYREL; return true;
    case SDLK_EQUALS: *out = AGC_KEY_PLUS; return true;
    case SDLK_MINUS: *out = AGC_KEY_MINUS; return true;
    default: return false;
    }
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s --rope PATH [options]\n"
            "  --rope PATH     rope image to run\n"
            "  --frames N      run N frames and exit, for a headless smoke test\n"
            "  --dump-dsky     print the panel when the run ends\n"
            "  --screenshot P  write the panel to PNG P when the run ends\n"
            "\n"
            "keys: 0-9  V verb  N noun  Return enter  R reset  C clear\n"
            "      K key release  = plus  - minus  P proceed  Esc quit\n",
            argv0);
}

int main(int argc, char **argv)
{
    const char *rope = NULL;
    long frames = 0;
    bool dump = false;
    const char *shot = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--rope") == 0 && i + 1 < argc) {
            rope = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = strtol(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--dump-dsky") == 0) {
            dump = true;
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shot = argv[++i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (!rope) {
        usage(argv[0]);
        return 2;
    }

    agc *m = calloc(1, sizeof *m);
    if (!m) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    agc_init(m);
    if (agc_load_rope(m, rope) < 0) {
        fprintf(stderr, "cannot read rope %s\n", rope);
        free(m);
        return 1;
    }
    agc_cpu_start(m);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        free(m);
        return 1;
    }

    SDL_Window *w = NULL;
    SDL_Renderer *r = NULL;
    if (!SDL_CreateWindowAndRenderer("AGC DSKY", 940, 620, 0, &w, &r)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        SDL_Quit();
        free(m);
        return 1;
    }

    /* One frame of emulated time at 60 Hz. The machine runs at its own rate and
     * the frame rate decides only how often we look at it. */
    const unsigned mcts_per_frame = MCTS_PER_SECOND / 60u;

    bool running = true;
    for (long frame = 0; running && (frames == 0 || frame < frames); ++frame) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            enum agc_dsky_key k;
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (e.key.key == SDLK_P) {
                    agc_dsky_set_proceed(m, true);
                } else if (key_for(e.key.key, &k)) {
                    agc_dsky_press(m, k, 0);
                }
            } else if (e.type == SDL_EVENT_KEY_UP && e.key.key == SDLK_P) {
                agc_dsky_set_proceed(m, false);
            }
        }

        for (unsigned i = 0; i < mcts_per_frame; ++i) {
            agc_tick_mct(m);
        }

        draw_panel(r, &m->dsky);
        SDL_Delay(16);
    }

    if (shot) {
#ifdef AGC_HAVE_PNG
        if (screenshot(r, shot)) {
            printf("wrote %s\n", shot);
        }
#else
        fprintf(stderr, "built without libpng; --screenshot unavailable\n");
#endif
    }

    if (dump) {
        char line[256];
        agc_dsky_format(&m->dsky, line, sizeof line);
        printf("DSKY %s\n", line);
    }

    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    SDL_Quit();
    free(m);
    return 0;
}
