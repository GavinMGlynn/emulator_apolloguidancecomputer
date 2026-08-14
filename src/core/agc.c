#include "agc.h"

#include <stdio.h>
#include <string.h>

void agc_init(agc *m)
{
    memset(m, 0, sizeof *m);
    agc_memory_clear(&m->mem);
    agc_scaler_reset(&m->scaler);
    agc_dsky_reset(&m->dsky);
    agc_uplink_reset(&m->uplink);
    agc_cpu_start(m);
}

long agc_load_rope(agc *m, const char *path)
{
    return agc_memory_load_rope(&m->mem, path);
}

void agc_gojam(agc *m)
{
    agc_cpu_queue_gojam(&m->cpu);
}

void agc_tick(agc *m)
{
    agc_cpu_tick(m);
    m->timepulses++;

    /* The RUPT LOCK detector watches the interrupt-in-progress line, so it has
     * to be sampled every timing pulse, not every scaler tick. */
    agc_scaler_update_interrupt_state(&m->scaler, m->cpu.iip);

    /* TC TRAP watches for a program that only ever executes TC and TCF. The
     * "ended" half of the test fires at T4 of any MCT that is neither a
     * transfer of control nor a stolen counter cycle. */
    const char *name = m->cpu.subinst ? m->cpu.subinst->name : "";
    if (strcmp(name, "TC0") == 0 || strcmp(name, "TCF0") == 0) {
        m->scaler.tc_started = true;
    } else if (m->cpu.timepulse == 4 && !m->cpu.inkl) {
        m->scaler.tc_ended = true;
    }

    /* The scaler runs at a tenth of the timing-pulse rate. */
    if (m->timepulses % AGC_SCALER_DIVISOR == 0) {
        agc_scaler_tick(m);
    }

    /* After the scaler, so the DSKY samples this pulse's flash phase. */
    agc_dsky_tick(m);
    agc_uplink_tick(m);
}

void agc_tick_mct(agc *m)
{
    for (unsigned i = 0; i < AGC_TIMEPULSES_PER_MCT; ++i) {
        agc_tick(m);
    }
}

int agc_format_state(const agc *m, char *buf, size_t len)
{
    const agc_cpu *c = &m->cpu;
    return snprintf(buf, len,
                    "%-6s T%-2u A=%06o L=%06o Q=%06o Z=%06o G=%06o B=%06o "
                    "S=%04o SQ=%02o ST=%u BR=%u%u EB=%o FB=%02o BB=%06o "
                    "X=%06o Y=%06o U=%06o WL=%06o EXT=%d INH=%d IIP=%d INKL=%d",
                    c->subinst ? c->subinst->name : "-", c->timepulse,
                    c->a, c->l, c->q, c->z, c->g, c->b,
                    c->s, c->sq, c->st, (c->br >> 1) & 1u, c->br & 1u,
                    (unsigned)(c->eb >> 8), (unsigned)(c->fb >> 10), c->bb,
                    c->x, c->y, c->u, c->write_bus,
                    c->extend, c->inhibit_interrupts, c->iip, c->inkl);
}
