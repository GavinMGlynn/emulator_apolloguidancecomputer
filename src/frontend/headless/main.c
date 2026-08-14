/* Deterministic headless frontend.
 *
 * No wall clock, no host input, no threads: the only thing that advances time
 * is --timepulses / --mct, and identical arguments produce identical output on
 * every host and build type. This is the engine of the verification
 * methodology — probe goldens are its stdout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agc.h"

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [options]\n"
            "\n"
            "  --rope PATH          load a `yaYUL --hardware` core rope image\n"
            "  --rope-at BANK:PATH  load a rope-module dump at a bank offset\n"
            "  --mct N              run N memory cycle times (12 timing pulses each)\n"
            "  --timepulses N       run N timing pulses\n"
            "  --trace              print machine state after every timing pulse\n"
            "  --trace-mct          print machine state at the end of every MCT\n"
            "  --dump-state         print machine state when the run ends\n"
            "  --dump-mem A[:LEN]   dump LEN (default 1) erasable words from octal A\n"
            "  --dump-counters      print the counter cells and interrupt requests\n"
            "  --dump-channels      print every I/O channel\n"
            "  --dump-dsky          print the DSKY display when the run ends\n"
            "  --trace-dsky         print the DSKY display every time it changes\n"
            "  --press KEY:MCT      press a DSKY key at the given MCT. KEY is a\n"
            "                       digit, V, N, E (enter), R (reset), C (clear),\n"
            "                       K (key release), + or -. Repeatable.\n"
            "  --uplink KEY:MCT     uplink the same key from the ground instead,\n"
            "                       as a triple-redundant word. Repeatable.\n"
            "  --module N:PATH      load a physical rope-module dump as module N\n"
            "                       (1-6). Repeatable; six of them make a rope.\n"
            "  --auto-proceed       press PRO whenever the program lights OPR ERR\n"
            "                       and waits, reporting the PROG/NOUN it stopped\n"
            "                       on. This is how the Validation suite is run:\n"
            "                       it stops at every checkpoint and at every\n"
            "                       failure, and PROG 77 means it finished.\n"
            "  --sentinel A         watch octal erasable cell A; report the MCT at\n"
            "                       which it first becomes non-zero. Repeatable.\n"
            "                       The run stops once every sentinel has fired.\n"
            "  --ignore-alarms      suppress the hardware alarms and their GOJAMs\n"
            "  --inhibit-alarm N    suppress one channel-77 alarm bit (octal mask)\n"
            "  --ignore-counters    never steal an MCT for a counter request\n"
            "  --ignore-interrupts  never take a program interrupt\n"
            "\n"
            "--dump-mem may be given more than once; dumps happen after the run.\n",
            argv0);
}

#define MAX_DUMPS 32
#define MAX_SENTINELS 64
#define MAX_PRESSES 64

/* Wait for the display to stop changing before reading it: ERRORDSP blanks
 * eleven relay banks and then writes the code, and the OPR ERR lamp goes on
 * before the digits are all in place. */
#define PRO_SETTLE_PULSES 20000u
#define PRO_HOLD_PULSES   20000u

/* A scripted keypress: which key, and the MCT to press it at. Menus are driven
 * this way rather than from a clock, so a run is reproducible. */
struct press {
    enum agc_dsky_key key;
    unsigned long long mct;
    bool done;
    bool uplinked; /* from the ground rather than from the keyboard */
};

/* One character per key, chosen to read like the panel: the digits, then V, N,
 * E(nter), R(eset), C(lear), K(ey release), + and -. */
static bool parse_press(const char *spec, struct press *out)
{
    if (!spec) {
        return false;
    }
    const char *colon = strchr(spec, ':');
    if (!colon || colon == spec) {
        return false;
    }
    static const struct { char c; enum agc_dsky_key key; } keys[] = {
        { '0', AGC_KEY_0 }, { '1', AGC_KEY_1 }, { '2', AGC_KEY_2 },
        { '3', AGC_KEY_3 }, { '4', AGC_KEY_4 }, { '5', AGC_KEY_5 },
        { '6', AGC_KEY_6 }, { '7', AGC_KEY_7 }, { '8', AGC_KEY_8 },
        { '9', AGC_KEY_9 }, { 'V', AGC_KEY_VERB }, { 'N', AGC_KEY_NOUN },
        { 'E', AGC_KEY_ENTR }, { 'R', AGC_KEY_RSET }, { 'C', AGC_KEY_CLR },
        { 'K', AGC_KEY_KEYREL }, { '+', AGC_KEY_PLUS }, { '-', AGC_KEY_MINUS },
    };
    for (size_t i = 0; i < sizeof keys / sizeof *keys; ++i) {
        if (keys[i].c == spec[0] && colon == spec + 1) {
            out->key = keys[i].key;
            out->mct = strtoull(colon + 1, NULL, 0);
            out->done = false;
            return true;
        }
    }
    return false;
}

struct dump {
    unsigned addr;
    unsigned len;
};

/* A probe signals "I have reached this point" by storing a non-zero word in an
 * erasable cell. The AGC has no software-readable fine-grained cycle counter —
 * its finest is TIME6 at 1.6 kHz, some 53 MCTs per tick — so a probe cannot
 * time itself the way a machine with a cycle counter can. The harness times it
 * instead: exact, deterministic, and identical on every host, because what is
 * being recorded is an emulated timing-pulse count and not a measurement of
 * anything on this computer. See tools/probes/README.md. */
struct sentinel {
    unsigned addr;
    unsigned long long timepulse; /* 0 until it fires */
    bool fired;
};

static bool parse_octal(const char *s, unsigned *out)
{
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 8);
    if (end == s || *end != '\0') {
        return false;
    }
    *out = (unsigned)v;
    return true;
}

int main(int argc, char **argv)
{
    agc *m = calloc(1, sizeof *m);
    if (!m) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    agc_init(m);

    const char *rope = NULL;
    const char *rope_at = NULL;
    unsigned long pulses = 0;
    bool trace = false, trace_mct = false, dump_state = false;
    bool dump_counters = false, dump_channels = false;
    bool dump_dsky = false, trace_dsky = false, auto_proceed = false;
    bool loaded_module = false;
    struct press presses[MAX_PRESSES];
    size_t press_count = 0;
    struct dump dumps[MAX_DUMPS];
    size_t dump_count = 0;
    struct sentinel sentinels[MAX_SENTINELS];
    size_t sentinel_count = 0;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        bool needs_value = strcmp(a, "--rope") == 0 || strcmp(a, "--rope-at") == 0
                           || strcmp(a, "--mct") == 0 || strcmp(a, "--timepulses") == 0
                           || strcmp(a, "--dump-mem") == 0
                           || strcmp(a, "--inhibit-alarm") == 0
                           || strcmp(a, "--sentinel") == 0;
        if (needs_value && i + 1 >= argc) {
            fprintf(stderr, "%s needs a value\n", a);
            free(m);
            return 2;
        }

        if (strcmp(a, "--rope") == 0) {
            rope = argv[++i];
        } else if (strcmp(a, "--rope-at") == 0) {
            rope_at = argv[++i];
        } else if (strcmp(a, "--mct") == 0) {
            pulses += strtoul(argv[++i], NULL, 0) * AGC_TIMEPULSES_PER_MCT;
        } else if (strcmp(a, "--timepulses") == 0) {
            pulses += strtoul(argv[++i], NULL, 0);
        } else if (strcmp(a, "--trace") == 0) {
            trace = true;
        } else if (strcmp(a, "--trace-mct") == 0) {
            trace_mct = true;
        } else if (strcmp(a, "--dump-state") == 0) {
            dump_state = true;
        } else if (strcmp(a, "--dump-counters") == 0) {
            dump_counters = true;
        } else if (strcmp(a, "--dump-channels") == 0) {
            dump_channels = true;
        } else if (strcmp(a, "--dump-dsky") == 0) {
            dump_dsky = true;
        } else if (strcmp(a, "--trace-dsky") == 0) {
            trace_dsky = true;
        } else if (strcmp(a, "--module") == 0) {
            const char *spec = argv[++i];
            const char *colon = spec ? strchr(spec, ':') : NULL;
            if (!colon) {
                fprintf(stderr, "--module wants N:PATH, e.g. 1:bios/rope-modules/x.bin\n");
                free(m);
                return 2;
            }
            unsigned n = (unsigned)strtoul(spec, NULL, 0);
            long words = agc_memory_load_module(&m->mem, colon + 1, n);
            if (words < 0) {
                fprintf(stderr, "cannot read rope module %s\n", colon + 1);
                free(m);
                return 1;
            }
            fprintf(stderr, "loaded %ld words as module %u from %s\n", words, n, colon + 1);
            loaded_module = true;
        } else if (strcmp(a, "--auto-proceed") == 0) {
            auto_proceed = true;
        } else if (strcmp(a, "--press") == 0) {
            if (press_count == MAX_PRESSES || !parse_press(argv[++i], &presses[press_count])) {
                fprintf(stderr, "--press wants KEY:MCT, e.g. V:1200\n");
                free(m);
                return 2;
            }
            press_count++;
        } else if (strcmp(a, "--uplink") == 0) {
            if (press_count == MAX_PRESSES || !parse_press(argv[++i], &presses[press_count])) {
                fprintf(stderr, "--uplink wants KEY:MCT, e.g. V:1200\n");
                free(m);
                return 2;
            }
            presses[press_count].uplinked = true;
            press_count++;
        } else if (strcmp(a, "--sentinel") == 0) {
            unsigned addr = 0;
            if (sentinel_count == MAX_SENTINELS) {
                fprintf(stderr, "too many --sentinel requests\n");
                free(m);
                return 2;
            }
            if (!parse_octal(argv[++i], &addr) || addr >= AGC_ERASABLE_WORDS) {
                fprintf(stderr, "--sentinel wants an octal erasable address\n");
                free(m);
                return 2;
            }
            sentinels[sentinel_count++] = (struct sentinel){ addr, 0, false };
        } else if (strcmp(a, "--ignore-alarms") == 0) {
            m->ignore_alarms = true;
        } else if (strcmp(a, "--inhibit-alarm") == 0) {
            unsigned mask = 0;
            if (!parse_octal(argv[++i], &mask)) {
                fprintf(stderr, "--inhibit-alarm wants an octal mask\n");
                free(m);
                return 2;
            }
            m->alarm_inhibit = agc_w(m->alarm_inhibit | mask);
        } else if (strcmp(a, "--ignore-counters") == 0) {
            m->ignore_counters = true;
        } else if (strcmp(a, "--ignore-interrupts") == 0) {
            m->ignore_interrupts = true;
        } else if (strcmp(a, "--dump-mem") == 0) {
            if (dump_count == MAX_DUMPS) {
                fprintf(stderr, "too many --dump-mem requests\n");
                free(m);
                return 2;
            }
            char spec[64];
            snprintf(spec, sizeof spec, "%s", argv[++i]);
            char *colon = strchr(spec, ':');
            unsigned len = 1;
            if (colon) {
                *colon = '\0';
                len = (unsigned)strtoul(colon + 1, NULL, 0);
            }
            unsigned addr = 0;
            if (!parse_octal(spec, &addr)) {
                fprintf(stderr, "--dump-mem address must be octal: %s\n", spec);
                free(m);
                return 2;
            }
            dumps[dump_count++] = (struct dump){ addr, len };
        } else {
            usage(argv[0]);
            free(m);
            return 2;
        }
    }

    if (rope) {
        long words = agc_load_rope(m, rope);
        if (words < 0) {
            fprintf(stderr, "cannot read rope %s\n", rope);
            free(m);
            return 1;
        }
        fprintf(stderr, "loaded %ld words from %s\n", words, rope);
    }
    if (rope_at) {
        char spec[512];
        snprintf(spec, sizeof spec, "%s", rope_at);
        char *colon = strchr(spec, ':');
        if (!colon) {
            fprintf(stderr, "--rope-at wants BANK:PATH\n");
            free(m);
            return 2;
        }
        *colon = '\0';
        unsigned bank = (unsigned)strtoul(spec, NULL, 0);
        long words = agc_memory_load_rope_at(&m->mem, colon + 1, bank);
        if (words < 0) {
            fprintf(stderr, "cannot read rope module %s\n", colon + 1);
            free(m);
            return 1;
        }
        fprintf(stderr, "loaded %ld words at bank %u from %s\n", words, bank, colon + 1);
    }

    /* Loading the rope after agc_init means the power-on GOJAM has already run
     * against empty fixed memory. Run it again so the first fetch sees the
     * rope, exactly as a machine powered up with its ropes installed does. */
    if (rope || rope_at || loaded_module) {
        agc_cpu_start(m);
    }

    char line[256];
    char dsky_line[256], dsky_prev[256] = "";
    bool pro_held = false;
    unsigned pro_settle = 0, pro_hold = 0;
    unsigned long long stops = 0;
    size_t fired = 0;
    for (unsigned long i = 0; i < pulses; ++i) {
        for (size_t k = 0; k < press_count; ++k) {
            if (!presses[k].done
                && m->timepulses / AGC_TIMEPULSES_PER_MCT >= presses[k].mct) {
                presses[k].done = true;
                if (presses[k].uplinked) {
                    agc_uplink_send(m, agc_uplink_keycode((unsigned)presses[k].key));
                } else {
                    agc_dsky_press(m, presses[k].key, 0);
                }
            }
        }
        agc_tick(m);

        /* The Validation suite reports through the panel, not through memory:
         * it puts a code in PROG and a sub-code in NOUN, lights OPR ERR and
         * waits for PRO. Pressing it here turns "the machine did not alarm"
         * into a list of what the suite actually said. */
        if (auto_proceed) {
            const bool waiting = agc_dsky_lamp(&m->dsky, AGC_DSKY_LAMP_OPR_ERR);
            if (waiting && !pro_held) {
                if (++pro_settle >= PRO_SETTLE_PULSES) {
                    unsigned p1 = agc_dsky_digit(&m->dsky, AGC_DSKY_PROG1);
                    unsigned p2 = agc_dsky_digit(&m->dsky, AGC_DSKY_PROG2);
                    unsigned n1 = agc_dsky_digit(&m->dsky, AGC_DSKY_NOUN1);
                    unsigned n2 = agc_dsky_digit(&m->dsky, AGC_DSKY_NOUN2);
                    printf("STOP %llu PROG %u%u NOUN %u%u\n",
                           (unsigned long long)(m->timepulses / AGC_TIMEPULSES_PER_MCT),
                           p1 == AGC_DSKY_BLANK ? 0u : p1, p2 == AGC_DSKY_BLANK ? 0u : p2,
                           n1 == AGC_DSKY_BLANK ? 0u : n1, n2 == AGC_DSKY_BLANK ? 0u : n2);
                    agc_dsky_set_proceed(m, true);
                    pro_held = true;
                    pro_settle = 0;
                    stops++;
                }
            } else if (!waiting) {
                pro_settle = 0;
                if (pro_held) {
                    /* Hold it long enough for the program's own polling loop to
                     * see it, then let go — it waits for the release too. */
                    if (++pro_hold >= PRO_HOLD_PULSES) {
                        agc_dsky_set_proceed(m, false);
                        pro_held = false;
                        pro_hold = 0;
                    }
                }
            }
        }

        if (trace_dsky) {
            agc_dsky_format(&m->dsky, dsky_line, sizeof dsky_line);
            if (strcmp(dsky_line, dsky_prev) != 0) {
                printf("DSKY %llu  %s\n",
                       (unsigned long long)(m->timepulses / AGC_TIMEPULSES_PER_MCT),
                       dsky_line);
                snprintf(dsky_prev, sizeof dsky_prev, "%s", dsky_line);
            }
        }
        if (trace || (trace_mct && m->cpu.timepulse == 1)) {
            agc_format_state(m, line, sizeof line);
            printf("%s\n", line);
        }
        for (size_t s = 0; s < sentinel_count; ++s) {
            if (!sentinels[s].fired && m->mem.erasable[sentinels[s].addr] != 0) {
                sentinels[s].fired = true;
                sentinels[s].timepulse = m->timepulses;
                fired++;
            }
        }
        /* --mct is an upper bound for a probe run, not the intent: stop as soon
         * as the probe has said everything it was going to say, so a golden
         * cannot accidentally depend on how long the parking loop ran. */
        if (sentinel_count && fired == sentinel_count) {
            break;
        }
    }

    for (size_t s = 0; s < sentinel_count; ++s) {
        if (sentinels[s].fired) {
            printf("SENT %04o %llu %llu\n", sentinels[s].addr,
                   sentinels[s].timepulse / AGC_TIMEPULSES_PER_MCT,
                   sentinels[s].timepulse);
        } else {
            printf("SENT %04o never\n", sentinels[s].addr);
        }
    }

    if (dump_state) {
        agc_format_state(m, line, sizeof line);
        printf("%s\n", line);
        printf("timepulses=%llu mct=%llu alarm=%d\n",
               (unsigned long long)m->timepulses,
               (unsigned long long)(m->timepulses / AGC_TIMEPULSES_PER_MCT),
               m->alarm_latched);
    }

    for (size_t d = 0; d < dump_count; ++d) {
        for (unsigned k = 0; k < dumps[d].len; ++k) {
            unsigned addr = dumps[d].addr + k;
            if (addr >= AGC_ERASABLE_WORDS) {
                break;
            }
            printf("E %04o %06o\n", addr, m->mem.erasable[addr]);
        }
    }

    if (auto_proceed) {
        printf("STOPS %llu\n", stops);
    }

    if (dump_dsky) {
        agc_dsky_format(&m->dsky, dsky_line, sizeof dsky_line);
        printf("DSKY %s\n", dsky_line);
        printf("DSKY relay-writes %llu\n",
               (unsigned long long)m->dsky.relay_writes);
        printf("DSKY lamps %06o status %06o\n", m->dsky.lamps, m->dsky.status);
        printf("UPLINK accepted %llu refused %llu words %llu\n",
               (unsigned long long)m->uplink.bits_accepted,
               (unsigned long long)m->uplink.bits_refused,
               (unsigned long long)m->uplink.words_sent);
        for (unsigned b = 0; b < AGC_DSKY_BANKS; ++b) {
            printf("DSKY relay %02o %04o\n", b, m->dsky.relay[b]);
        }
    }

    if (dump_channels) {
        for (unsigned n = 0; n < AGC_CHANNEL_COUNT; ++n) {
            printf("CH %02o %06o\n", n, agc_cpu_read_channel(m, n));
        }
    }

    if (dump_counters) {
        for (unsigned i = 0; i < AGC_COUNTER_COUNT; ++i) {
            printf("CNT %02o %u\n", i + AGC_COUNTER_BASE, m->cpu.counters[i]);
        }
        for (unsigned i = 0; i < AGC_RUPT_COUNT; ++i) {
            printf("RUPT %u %d\n", i, m->cpu.interrupts[i]);
        }
    }

    free(m);
    return 0;
}
