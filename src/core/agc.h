/* The machine: a Block II Apollo Guidance Computer.
 *
 * This is the whole public surface of the core. The core is a static library
 * with no frontend dependencies of any kind — no I/O, no clock, no threads.
 * Time advances only when someone calls agc_tick(), and one call is exactly one
 * timing pulse (1/1.024 MHz, so 12 calls per Memory Cycle Time).
 */
#ifndef AGC_H
#define AGC_H

#include <stdbool.h>
#include <stdint.h>

#include "agc_word.h"
#include "cpu/cpu.h"
#include "dsky/dsky.h"
#include "io/channels.h"
#include "io/counters.h"
#include "io/uplink.h"
#include "memory/memory.h"
#include "peripherals/cdu.h"
#include "peripherals/imu.h"
#include "peripherals/telemetry.h"
#include "timing/scaler.h"

/* Clock constants, from the timer sheets. */
#define AGC_MASTER_CLOCK_HZ    2048000u
#define AGC_TIMEPULSE_CLOCK_HZ 1024000u
#define AGC_TIMEPULSES_PER_MCT 12u
/* 11.71875 us, expressed exactly in picoseconds so nothing here is floating
 * point: emulated time must be bit-identical on every host. */
#define AGC_MCT_PICOSECONDS    11718750ull

typedef struct agc {
    agc_cpu cpu;
    agc_memory mem;
    agc_channels channels;
    agc_scaler scaler;
    agc_dsky dsky;
    agc_uplink uplink;
    agc_cdu cdu;
    agc_imu imu;
    agc_telemetry telemetry;

    /* Timing pulses since reset. Divided by 12 this is MCTs; the scaler is
     * clocked from it every AGC_SCALER_DIVISOR pulses, counted down here
     * rather than derived, because agc_tick is the hottest path there is. */
    uint64_t timepulses;
    unsigned scaler_countdown;

    /* Set when a hardware alarm fires. The frontend decides what to do about
     * it; the core just records the reason in channel 77 and GOJAMs. */
    bool alarm_latched;

    /* Test/inspection hooks. These suppress hardware behaviour and so are off
     * by default; a probe that sets one must say so in its golden.
     *
     * `alarm_inhibit` holds channel-77 alarm bits that should not fire, which
     * is how a test isolates one alarm from the others — an idle machine trips
     * RUPT LOCK at ~300 ms and would otherwise always beat the night watchman
     * to it. `ignore_alarms` is the blanket form. */
    agc_word alarm_inhibit;
    bool ignore_alarms;
    bool ignore_counters;
    bool ignore_interrupts;
} agc;

/* Zero the machine, clear memory, and run the power-on GOJAM. Call
 * agc_load_rope() first if you want the rope in place before the first fetch,
 * which you almost always do — GOJ1 jumps to 04000 immediately. */
void agc_init(agc *m);

/* Advance exactly one timing pulse: the CPU's three phases, then the scaler if
 * this pulse lands on a scaler edge. */
void agc_tick(agc *m);

/* Convenience: advance a whole Memory Cycle Time (12 timing pulses). */
void agc_tick_mct(agc *m);

/* Load a `yaYUL --hardware` rope image into fixed memory. Returns words read,
 * or -1. */
long agc_load_rope(agc *m, const char *path);

/* Request the hardware restart. */
void agc_gojam(agc *m);

/* A one-line summary of machine state, for tracing and for golden dumps. The
 * format is stable: probe goldens diff it. */
int agc_format_state(const agc *m, char *buf, size_t len);

#endif /* AGC_H */
