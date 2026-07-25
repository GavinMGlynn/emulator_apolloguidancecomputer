#!/usr/bin/env python3
"""Generate the mid-instruction integrity probe.

A counter request can be raised at any timing pulse, but it may only be
*serviced* at the end of a subinstruction chain — the sequence generator checks
for pending counters at T12, when the next instruction would be fetched. So a
multi-MCT instruction is never split down the middle: a request that arrives
during the fourth MCT of a divide waits for the divide to finish.

That is easy to state and easy to get wrong, and the failure mode is nasty. If
an involuntary sequence were spliced in mid-instruction it would overwrite S, G
and the adder — every register a divide is using to hold its partial state — and
the divide would return a wrong answer. Not a crash. Just an occasional wrong
number, under counter load, in the middle of a descent.

The probe checks it the only way that really counts: by doing arithmetic and
looking at the answer. It runs the same loop of 40 multiply/divide round trips
twice, once with the machine quiet and once under continuous counter traffic
from TIME6, and requires both to produce the same right answer.

    A = 7
    repeat 40 times:
        EXTEND ; MP FIVE     -> A,L = 7 x 5 = 35   (A = 0, L = 35)
        EXTEND ; DV FIVE     -> A = 35 / 5 = 7, L = remainder

Multiply and divide are the right instruments because they are the longest
instructions in the machine — 3 and 6 MCTs — so they present the widest target
for a badly-timed steal, and because they carry more live state between MCTs
than anything else (the divide stage counter, the partial remainder in L, the
end-around-carry inhibit).

The probe also proves the traffic was real: TIME6 must have counted down, which
it can only do by stealing cycles.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from asm import Asm, SCRATCH_START  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent
OUT_BIN = ROOT / "tests/probes/integrity.bin"
OUT_META = ROOT / "tests/probes/integrity.meta"

CH_MISC = 0o13
TIME6 = 0o24 + 5              # 0031

FIVE_E = SCRATCH_START + 0    # 0100  DV addresses erasable only
RES_QUIET = SCRATCH_START + 1 # 0101  result with the machine to itself
RES_BUSY = SCRATCH_START + 2  # 0102  ... and under counter traffic
DONE = SCRATCH_START + 3      # 0103
ACC = SCRATCH_START + 4       # 0104  the running value, across loop iterations
CTR = SCRATCH_START + 5       # 0105  loop counter

SEED = 7
FACTOR = 5
ROUND_TRIPS = 40

# TIME6 counts down at 1.6 kHz. Preset it high so it never reaches zero: on
# arrival it would clear its own arming bit and the traffic would stop.
T6_PRESET = 0o7777


def build():
    a = Asm()

    one = a.label("one")
    seed = a.label("seed")
    count = a.label("count")
    five = a.label("five")
    arm_bit = a.label("arm_bit")
    t6_preset = a.label("t6_preset")

    def round_trips():
        """A counted loop, not a straight run of arithmetic.

        The first version of this probe unrolled 60 round trips into 240
        consecutive instructions with no transfer of control anywhere — and
        tripped TC TRAP, because the hardware wants to see both a TC and a
        non-TC inside every ~5 ms window and concludes otherwise that the
        program counter is stuck. That was the emulator being right and the
        probe being unrealistic; no AGC program runs 240 instructions without
        a branch. Looping also keeps the accumulator's running value in
        erasable across iterations, since CCS would clobber it.
        """
        a.ca(count)
        a.ts(CTR)
        a.ca(seed)
        a.ts(ACC)

        top = a.here()
        a.ca(ACC)
        a.extend()
        a.mp(five)
        a.extend()
        a.dv(FIVE_E)
        a.ts(ACC)

        dec = a.here() + 5
        done = a.here() + 7
        a.ccs(CTR)
        a.tcf(dec)    # positive
        a.tcf(done)   # +0
        a.tcf(dec)    # negative
        a.tcf(done)   # -0
        assert a.here() == dec, f"{oct(a.here())} != {oct(dec)}"
        a.ts(CTR)
        a.tcf(top)
        assert a.here() == done, f"{oct(a.here())} != {oct(done)}"
        a.ca(ACC)

    # DV takes its divisor from erasable; MP can read fixed directly.
    a.ca(five)
    a.ts(FIVE_E)

    # --- the machine to itself ----------------------------------------------
    round_trips()
    a.ts(RES_QUIET)

    # --- continuous counter traffic ------------------------------------------
    a.ca(t6_preset)
    a.ts(TIME6)
    a.ca(arm_bit)
    a.extend()
    a.write(CH_MISC)

    round_trips()
    a.ts(RES_BUSY)

    a.ca(one)
    a.ts(DONE)

    park = a.label("park")
    a.tcf(park)

    one.addr = a.here();       a.word(1)
    seed.addr = a.here();      a.word(SEED)
    count.addr = a.here();     a.word(ROUND_TRIPS)
    five.addr = a.here();      a.word(FACTOR)
    arm_bit.addr = a.here();   a.word(0o40000)   # channel bit 16, via bit 15
    t6_preset.addr = a.here(); a.word(T6_PRESET)

    return a


def main() -> int:
    a = build()
    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    a.write_rope(str(OUT_BIN))

    lines = [
        "# Generated by tools/probes/gen_integrity_probe.py — do not edit.",
        "# A value probe. Counters stay ENABLED: the traffic is the experiment.",
        f"stop_at {DONE:o}",
        "mct 20000",
        "",
        f"# {ROUND_TRIPS} multiply/divide round trips return the seed unchanged,",
        "# both with the machine quiet and under continuous counter traffic. A",
        "# counter serviced mid-instruction would overwrite S, G and the adder —",
        "# every register a divide holds its partial state in — and the answer",
        "# would come back wrong.",
        f"expect_mem quiet_arithmetic_is_right {RES_QUIET:o} {SEED:o}",
        f"expect_mem busy_arithmetic_is_right {RES_BUSY:o} {SEED:o}",
        "",
        "# And the traffic was real: TIME6 can only have counted down by",
        "# stealing memory cycles from the program.",
        f"expect_mem_max counter_traffic_occurred {TIME6:o} {T6_PRESET - 1:o}",
    ]
    OUT_META.write_text("\n".join(lines) + "\n")

    print(f"wrote {OUT_BIN.relative_to(ROOT)} and {OUT_META.relative_to(ROOT)}: "
          f"{a.here() - 0o4000} words")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
