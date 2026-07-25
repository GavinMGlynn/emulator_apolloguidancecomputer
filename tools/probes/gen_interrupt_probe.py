#!/usr/bin/env python3
"""Generate the interrupt-discipline probe.

Two rules the AGC enforces in hardware, both invisible to any unit test and both
easy to get subtly wrong:

**An interrupt is refused while the accumulator holds overflow.** A is not saved
by the interrupt sequence — only Z and B are, into ZRUPT and BRUPT — so the
handler is free to clobber it, and the flight software's handlers do. But an
overflowed A is a 16-bit quantity whose top two bits disagree, and the moment it
passes through erasable memory the overflow is lost. So the sequence generator
simply will not break in until the program has resolved it. Get this wrong and
the failure is not a crash; it is arithmetic that goes quietly wrong once in a
long while under interrupt load, which is the worst kind of bug this machine can
have.

**RESUME puts the program back exactly.** RSM3 reloads Z from ZRUPT and B from
BRUPT and drops the interrupt-in-progress line. Miss any part of it and the
machine either resumes in the wrong place or — as this emulator did before
FINDINGS #16 — never takes another interrupt again.

The probe runs the same experiment twice, which is what makes it conclusive:

    phase 1   arm TIME6, put overflow in A, RELINT
              100 x INCR CTR      <- A untouched, overflow persists,
                                     the pending interrupt must be refused
              TS SCRATCH          <- resolves the overflow (and skips)
              10 x INCR CTR

    phase 2   re-arm TIME6, reset CTR, RELINT, A clean
              100 x INCR CTR      <- nothing to refuse

The handler records the value of CTR at the moment it runs. In phase 1 that
number must be at least 100: the interrupt was requested part-way through the
run and held off until the very end. In phase 2, with an identical run and an
identical interrupt source, it must be well under 100. Phase 2 is the control —
without it, a phase-1 result of "100" would be equally consistent with the
interrupt simply having arrived late.

That RESUME worked is asserted separately: after phase 1 the program has to go
on and finish all 110 of its increments, which it can only do if Z came back
from ZRUPT intact.

INCR is the loop body precisely because it does not touch A. A CCS countdown
would have been tidier and would also have destroyed the overflow it was
supposed to be protecting.

Counters are deliberately left enabled: TIME6 is the interrupt source.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from asm import Asm, SCRATCH_START  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent
OUT_BIN = ROOT / "tests/probes/interrupts.bin"
OUT_META = ROOT / "tests/probes/interrupts.meta"

CH_MISC = 0o13           # bit 16 arms the TIME6 countdown
TIME6 = 0o24 + 5         # 0031
ZRUPT = 0o15             # where the interrupt sequence saves Z
BRUPT = 0o17             # ... and B

CTR = SCRATCH_START + 0       # 0100  increments actually executed this phase
FLAG = SCRATCH_START + 1      # 0101  set by the handler
SNAP = SCRATCH_START + 2      # 0102  CTR at the most recent handler entry
SCRATCH = SCRATCH_START + 3   # 0103  target of the overflow-resolving TS
SAVED_Z = SCRATCH_START + 4   # 0104  the handler's copy of ZRUPT
SAVED_B = SCRATCH_START + 5   # 0105  ... and of BRUPT
DONE = SCRATCH_START + 6      # 0106  stops the run
PHASE = SCRATCH_START + 7     # 0107  0 = overflow held, 1 = control
SNAP1 = SCRATCH_START + 0o10  # 0110  CTR when the handler ran in phase 1
SNAP2 = SCRATCH_START + 0o11  # 0111  ... and in phase 2
TOTAL1 = SCRATCH_START + 0o12 # 0112  CTR at the end of phase 1

VECTOR_T6RUPT = 0o4004        # interrupt vectors are at 04000 + 4n; T6RUPT is n=1
VECTOR_LAST = 0o4050          # RUPT10, the highest

OVERFLOW_INCRS = 100
TAIL_INCRS = 10
CONTROL_INCRS = 100


def build():
    a = Asm()

    one = a.label("one")
    zero = a.label("zero")
    big = a.label("big")
    arm_bit = a.label("arm_bit")
    t6_preset = a.label("t6_preset")

    # --- 04000: GOJAM lands here, and the vector table follows ---------------
    main = a.label("main_fwd")
    a.tcf(main)

    # Every vector must be woven, or a stray interrupt would fetch a word with
    # bad parity and restart the machine instead of being handled.
    park = a.label("park_fwd")
    while a.here() <= VECTOR_LAST:
        a.tcf(park)

    # --- the T6RUPT handler --------------------------------------------------
    # A is not saved by the interrupt sequence, so the handler may clobber it
    # freely — which is exactly why the hardware refuses to interrupt a program
    # holding overflow there.
    handler = a.here()
    a.ca(CTR)
    a.ts(SNAP)
    a.ccs(PHASE)
    #   +0/+1 branch words          ph2:  CA SNAP / TS SNAP2 / TCF fin
    #                               ph1:  CA SNAP / TS SNAP1
    #                               fin:  ...
    ph2 = a.here() + 4
    ph1 = ph2 + 3
    fin = ph1 + 2
    a.tcf(ph2)    # positive: phase 2 (PHASE = 1)
    a.tcf(ph1)    # +0:       phase 1
    a.tcf(ph1)    # negative: cannot happen
    a.tcf(ph1)    # -0:       cannot happen
    assert a.here() == ph2, f"{oct(a.here())} != {oct(ph2)}"
    a.ca(SNAP)
    a.ts(SNAP2)
    a.tcf(fin)
    assert a.here() == ph1, f"{oct(a.here())} != {oct(ph1)}"
    a.ca(SNAP)
    a.ts(SNAP1)
    assert a.here() == fin, f"{oct(a.here())} != {oct(fin)}"
    a.ca(one)
    a.ts(FLAG)
    a.ca(ZRUPT)
    a.ts(SAVED_Z)
    a.ca(BRUPT)
    a.ts(SAVED_B)
    a.resume()

    def arm_time6():
        a.ca(t6_preset)
        a.ts(TIME6)
        a.ca(arm_bit)
        a.extend()
        a.write(CH_MISC)

    # --- phase 1: the accumulator holds overflow -----------------------------
    main.addr = a.here()

    a.inhint()
    arm_time6()

    # Positive overflow: the largest positive word added to itself leaves bits
    # 15 and 16 disagreeing, which is what the sequence generator tests.
    a.ca(big)
    a.ad(big)
    a.relint()

    for _ in range(OVERFLOW_INCRS):
        a.incr(CTR)

    # TS resolves the overflow: it stores the corrected value, leaves +-1 in A,
    # and skips. The TCF absorbs the skip either way.
    a.ts(SCRATCH)
    after = a.here() + 1
    a.tcf(after)

    for _ in range(TAIL_INCRS):
        a.incr(CTR)

    a.ca(CTR)
    a.ts(TOTAL1)

    # --- phase 2: the control, identical but with a clean accumulator --------
    a.inhint()
    a.ca(one)
    a.ts(PHASE)
    a.ca(zero)
    a.ts(CTR)
    arm_time6()
    a.relint()

    for _ in range(CONTROL_INCRS):
        a.incr(CTR)

    # Say "finished" so the harness stops here. Without it the probe would park
    # in a branch-to-itself, trip the TC TRAP alarm ~10 ms later, and restart —
    # re-running everything on top of the results we came to read. (Which is
    # exactly what it did the first time.)
    a.ca(one)
    a.ts(DONE)

    park.addr = a.here()
    a.tcf(park)

    one.addr = a.here();       a.word(1)
    zero.addr = a.here();      a.word(0)
    big.addr = a.here();       a.word(0o37777)   # largest positive 15-bit word
    arm_bit.addr = a.here();   a.word(0o40000)   # channel bit 16, via bit 15
    t6_preset.addr = a.here(); a.word(1)

    # Patch the vector so T6RUPT reaches the handler.
    saved = a.here()
    a.at(VECTOR_T6RUPT)
    a.tcf(handler)
    a.at(saved)

    return a


def main() -> int:
    a = build()
    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    a.write_rope(str(OUT_BIN))

    total1 = OVERFLOW_INCRS + TAIL_INCRS
    lines = [
        "# Generated by tools/probes/gen_interrupt_probe.py — do not edit.",
        "# A value probe: no timing windows, only what the machine computed.",
        "# Counters stay ENABLED — TIME6 is the interrupt source.",
        f"stop_at {DONE:o}",
        "mct 4000",
        "",
        "# The handler ran, and RESUME put the program back so phase 1 went on to",
        f"# complete all {total1} of its increments.",
        f"expect_mem handler_ran {FLAG:o} 1",
        f"expect_mem resume_restored_the_program {TOTAL1:o} {total1:o}",
        "",
        "# Phase 1: the interrupt was requested part-way through and refused for",
        "# the whole time the accumulator held overflow.",
        f"expect_mem_min refused_while_a_overflowed {SNAP1:o} {OVERFLOW_INCRS:o}",
        "",
        "# Phase 2, the control: same loop, same interrupt source, clean",
        "# accumulator. Taken promptly, which is what makes phase 1 evidence of",
        "# refusal rather than of an interrupt that merely arrived late.",
        f"expect_mem_max control_taken_promptly {SNAP2:o} {OVERFLOW_INCRS // 2:o}",
        "",
        "# What the interrupt sequence saved, and what the handler read back.",
        f"dump {ZRUPT:o}:1",
        f"dump {BRUPT:o}:1",
        f"dump {SAVED_Z:o}:1",
        f"dump {SAVED_B:o}:1",
    ]
    OUT_META.write_text("\n".join(lines) + "\n")

    print(f"wrote {OUT_BIN.relative_to(ROOT)} and {OUT_META.relative_to(ROOT)}: "
          f"{a.here() - 0o4000} words")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
