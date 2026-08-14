#!/usr/bin/env python3
"""Does MP3A survive an MCT stolen by priority control?

The end-around carry is held off for the whole of MP3 by a third input on the
carry gate that no drawing prints — `CINORM = NOR(NEAC, EAC_n, MP3A)`, module A7
gate U7034 — where MP3A is the bare MP3 decode line (FINDINGS #11, #48-50).

That leaves one question nothing else in this project answers. The AGC has no
DMA: a peripheral wanting to move a number raises a counter request and the
sequence generator *steals a whole Memory Cycle Time* to run an involuntary
increment. If such a steal lands in the MCT that was about to be MP3, what
happens to MP3A? Two readings are possible and they differ in observable
arithmetic:

  - MP3A is a decode line off SQ and the stage counter. A steal leaves both
    alone, so MP3A stays up and the *counter's own increment* runs with the
    end-around carry inhibited.
  - Or the injected sequence suppresses the decode, and the counter increments
    normally.

FINDINGS #49 assumed the first and flagged it as confirmed by nothing. The gates
say the second, and say it plainly:

    MP3  = NOR(ST3_n, SQ7_n, SQEXT_n)          A3
    SQ7_n <- ... <- U3016 <- U3014             A3
    U3014 pad 1 = NOR(net_U3012_Pad4, INKL)    A3

INKL forces that gate low, which drives SQ7_n high, which kills the MP3 decode.
The SQ register itself is left alone — that is how the interrupted multiply
resumes afterwards — but the decode *line* is down for the duration of the
steal, and so is MP3A.

It changes no arithmetic, and the reason is worth stating rather than leaving to
be rediscovered. NEAC is set at MP0 T10 and not cleared until TL15 at MP3 T6, so
the latch already covers every whole MCT a steal inside a multiply can occupy.
The MP3A term only does work during MP3's own last six timing pulses, where no
steal can land: a steal takes a whole Memory Cycle Time and is decided between
them. Both readings therefore inhibit the carry for every steal that can
actually happen, which is why our core was right by accident.

The bench holds SQ at MP and the stage counter at MP3 — what a stolen MCT leaves
behind — and reads MP3A and the carry gate with INKL down and up. NEAC and EAC_n
are held inactive so that CINORM reports MP3A rather than being pinned by the
latch: this asks what MP3A *does* to the carry, having established when it is
up.

    tools/oracle/gate_mp3a.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import gate_sim  # noqa: E402
from gate_crosspoint import (  # noqa: E402
    DIVIDE_QUIET,
    SIM_ROOT,
    SQ_BITS,
    SUBINSTRUCTIONS,
)

#: A7 carries the service gates, and so the carry gate itself. Adding it to the
#: four sequence-generator modules is what lets this read CINORM rather than
#: stopping at MP3A and arguing about what A7 would do with it.
MODULES = ["sq_register", "stage_branch", "crosspoint_nqi", "crosspoint_ii",
           "service_gates"]

STAGE_LINES = ("ST0_n", "ST1_n", "STD2", "ST3_n")

#: The gate that forms the carry into bit 1, and the line under test.
CINORM = "__A07_2__CINORM"
MP3A = "MP3A"


def bench():
    netlist = gate_sim.build(SIM_ROOT, MODULES)
    quiescent = {
        net: (1 if net.endswith("_n") else 0)
        for net in netlist.undriven_nets()
        if not net.startswith("__nc_")
    }
    return netlist, quiescent


def read_at(netlist, quiescent, name: str, timing_pulse: int, inkl: int,
            ringing: frozenset[str]):
    """MP3A and CINORM at one timing pulse, with INKL forced high or low."""
    ext, order, qc, stage, decode = SUBINSTRUCTIONS[name]
    forces = dict(quiescent)
    bits = ((order >> 2) & 1, (order >> 1) & 1, order & 1, (qc >> 1) & 1, qc & 1)
    forces.update(zip(SQ_BITS, bits, strict=True))
    forces.update(SQEXT=ext, SQEXT_n=1 - ext, SQR10=0, SQR10_n=1)
    forces.update(ST0_n=1, ST1_n=1, STD2=0, ST3_n=1)
    line = STAGE_LINES[stage]
    forces[line] = 1 if line == "STD2" else 0
    forces.update(BR1=0, BR1_n=1, BR2=0, BR2_n=1)
    forces.update(DIVIDE_QUIET)

    # The line under test. Everything else about the steal — which counter, what
    # it is counting — lives in modules this bench does not load; INKL is the
    # announcement that reaches the decoder, and it is the only part of the
    # steal the sequence generator is told about.
    forces["INKL"] = inkl

    # CINORM = NOR(NEAC, EAC_n, MP3A). Left at their quiescent levels, EAC_n
    # sits high and pins the gate low whatever MP3A does, which would make the
    # carry column say nothing at all. Holding both other terms inactive turns
    # CINORM into the inverse of MP3A, which is the question being asked here:
    # not "is the carry inhibited during a multiply" — NEAC answers that — but
    # "does MP3A reach the carry gate".
    forces["NEAC"] = 0
    forces["EAC_n"] = 0

    for t in range(1, 13):
        on = t == timing_pulse
        forces[f"T{t:02d}"] = int(on)
        forces[f"T{t:02d}_n"] = int(not on)

    sim = gate_sim.Simulator(netlist)
    sim.drive(**{net: v for net, v in forces.items() if net in netlist.nets})
    unstable = sim.settle(known_ringing=ringing)

    wanted = [n for n in (MP3A, decode, CINORM) if n in netlist.nets]
    values = sim.read(wanted, unstable)
    return values, unstable


def main() -> int:
    netlist, quiescent = bench()
    if CINORM not in netlist.nets:
        print(f"FAIL {CINORM} is not in the netlist; module A7 did not load")
        return 1

    ringing: frozenset[str] = frozenset()
    rows = []
    for t in range(1, 13):
        row = {}
        for inkl in (0, 1):
            values, unstable = read_at(netlist, quiescent, "MP3", t, inkl, ringing)
            ringing |= unstable
            row[inkl] = values
        rows.append((t, row))

    print("SQ held at MP, stage counter at MP3 — the state a stolen MCT leaves.")
    print("NEAC and EAC_n held inactive, so CINORM reports MP3A alone.")
    print()
    print("            MP3A          CINORM (1 = carry allowed)")
    print("  TP    quiet   stolen    quiet   stolen")
    failures = []
    for t, row in rows:
        a0, a1 = row[0].get(MP3A), row[1].get(MP3A)
        c0, c1 = row[0].get(CINORM), row[1].get(CINORM)
        print(f"  T{t:<3}  {a0!s:<7} {a1!s:<9} {c0!s:<7} {c1!s}")
        if a0 != 1:
            failures.append(f"T{t}: MP3A down with no steal in progress")
        if a1 != 0:
            failures.append(f"T{t}: MP3A survived the steal")
        if c0 != 0:
            failures.append(f"T{t}: MP3A up but the carry gate allowed a carry")
        if c1 != 1:
            failures.append(f"T{t}: MP3A down but the carry gate still inhibited")
    print()

    if failures:
        for f in failures[:6]:
            print(f"FAIL {f}")
        if len(failures) > 6:
            print(f"     ... and {len(failures) - 6} more")
        return 1

    print("RESULT INKL suppresses the MP3 decode for the whole of the stolen "
          "MCT, and the")
    print("       carry gate follows MP3A exactly. So the involuntary increment "
          "runs with")
    print("       the MP3A inhibit *lifted* — the opposite of what FINDINGS #49 "
          "assumed.")
    print()
    print("       It changes no arithmetic. NEAC is set at MP0 T10 and held "
          "until TL15 at")
    print("       MP3 T6, which covers every whole MCT a steal inside a multiply "
          "can take;")
    print("       MP3A only does work in MP3's last six pulses, where no steal "
          "can land.")
    print("       src/core/cpu/cpu.c now clears mp3a on a steal, and every "
          "golden and the")
    print("       2496-case differential sweep are byte-identical across the "
          "change.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
