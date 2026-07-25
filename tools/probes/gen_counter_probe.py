#!/usr/bin/env python3
"""Generate the counter-interference probe.

The AGC has no DMA. A peripheral that wants to move a number raises a request on
one of 29 counter cells, and the sequence generator *steals a whole Memory Cycle
Time from the program* to run an involuntary increment. The program is never
told. Push enough counter traffic at the machine and it stops making progress —
which is the Apollo 11 1201/1202 mechanism, and the single most important
emergent behaviour in this emulator.

No unit test can see it, because it is not a property of any instruction. This
probe measures it, from inside the machine, using the one counter the program
itself can turn on and off: TIME6 counts down at 1.6 kHz, but only while bit 16
of channel 13 is set.

        window A:  a fixed CCS countdown loop, TIME6 disarmed
        window B:  the same loop, TIME6 armed

B − A is the time the program lost to a peripheral it never interacted with.

Two things are asserted rather than merely recorded:

  * B > A. If counter servicing were free, the emulator would not be modelling
    priority control at all.
  * B − A is a whole number of timing pulses divisible by 12. A stolen cycle is
    a *whole* MCT — the involuntary sequence runs T1 to T12 like any other — so
    a fractional difference would mean the steal is being spliced in at the
    wrong granularity.

The exact figure is left to the golden, because there is no closed form for it:
the loop's own lengthening changes how many 1.6 kHz ticks fall inside it.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from asm import Asm, SCRATCH_START  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent
OUT_BIN = ROOT / "tests/probes/counters.bin"
OUT_META = ROOT / "tests/probes/counters.meta"

CH_MISC = 0o13          # bit 16 arms the TIME6 countdown
CH_GYRO = 0o14          # bits 10-15 drive six counters at 3.2 kHz
TIME6 = 0o24 + 5        # counter cells start at 024; TIME6 is the sixth

# The channel-14 drive counters, in counter-cell order. Setting a bit makes the
# scaler request a down-count on that cell 3200 times a second; six of them at
# once is the heaviest storm a program can raise against itself.
DRIVE_COUNTERS = [0o24 + n for n in (19, 20, 21, 22, 23, 24)]   # GYROD..SHAFTD
CH14_STORM_BITS = 0o77000   # bits 10-15

CTR = SCRATCH_START + 0        # 0100  loop counter
SENTINEL_BASE = SCRATCH_START + 0o20  # 0120

# 64 iterations at 6 MCTs each: ~390 MCTs, ~4.6 ms per window. Kept short on
# purpose. A program that never enables an interrupt source is restarted by the
# RUPT LOCK alarm after about 120 ms — correctly — and a restart part-way
# through re-runs the whole probe, so a close sentinel fires on the second pass
# and the window it reports is fiction. An earlier version of this probe used
# 256 iterations and reported 5597 stolen MCTs, about eleven times the real
# figure, for exactly that reason.
LOOP_ITERATIONS = 0o100


def countdown_loop(a: Asm, k) -> None:
    """A CCS countdown: 6 MCTs per iteration, and entirely self-contained.

    CCS leaves |CTR| − 1 in A and skips into one of four branch words according
    to the sign of what it read, so the loop is written the way the flight
    software writes it."""
    a.ca(k["iterations"])
    a.ts(CTR)

    # loop_top:  CCS CTR      <- reads the counter, leaves |CTR|-1 in A
    #            TCF decr     <- +
    #            TCF done     <- +0
    #            TCF decr     <- -
    #            TCF done     <- -0
    # decr:      TS  CTR      <- store the diminished value back
    #            TCF loop_top
    # done:
    loop_top = a.here()
    decr = loop_top + 5
    done = loop_top + 7

    a.ccs(CTR)
    a.tcf(decr)
    a.tcf(done)
    a.tcf(decr)
    a.tcf(done)
    assert a.here() == decr, f"{oct(a.here())} != {oct(decr)}"
    a.ts(CTR)
    a.tcf(loop_top)
    assert a.here() == done, f"{oct(a.here())} != {oct(done)}"


def build():
    a = Asm()

    iterations = a.label("iterations")
    storm_bits = a.label("storm_bits")
    arm_bit = a.label("arm_bit")
    time6_preset = a.label("time6_preset")
    zero = a.label("zero")
    k = {"iterations": iterations, "arm_bit": arm_bit,
         "time6_preset": time6_preset, "zero": zero}

    sa1, sb1 = SENTINEL_BASE + 0, SENTINEL_BASE + 1
    sa2, sb2 = SENTINEL_BASE + 2, SENTINEL_BASE + 3
    sa3, sb3 = SENTINEL_BASE + 4, SENTINEL_BASE + 5

    # --- window A: the loop with no counter traffic --------------------------
    a.ca(iterations)
    a.ts(sa1)
    countdown_loop(a, k)
    a.ca(iterations)
    a.ts(sb1)

    # --- arm TIME6 -----------------------------------------------------------
    # Preset the counter well clear of zero: DINC counts *toward* zero, and on
    # arrival it clears the arming bit and raises T6RUPT, which would end the
    # experiment early and vector into unwoven rope.
    a.ca(time6_preset)
    a.ts(TIME6)
    a.ca(arm_bit)
    a.extend()
    a.write(CH_MISC)

    # --- window B: the identical loop, now competing with TIME6 --------------
    a.ca(iterations)
    a.ts(sa2)
    countdown_loop(a, k)
    a.ca(iterations)
    a.ts(sb2)

    # --- window C: the storm -------------------------------------------------
    # Six more counters, each requesting a down-count at 3.2 kHz. Preset them
    # clear of zero first: a DINC that arrives at zero clears the channel bit
    # driving it, and the storm would taper off as each one arrived.
    for cell in DRIVE_COUNTERS:
        a.ca(time6_preset)
        a.ts(cell)
    a.ca(storm_bits)
    a.extend()
    a.write(CH_GYRO)

    a.ca(iterations)
    a.ts(sa3)
    countdown_loop(a, k)
    a.ca(iterations)
    a.ts(sb3)

    park = a.label("park")
    a.tcf(park)

    iterations.addr = a.here();   a.word(LOOP_ITERATIONS)
    storm_bits.addr = a.here();   a.word(CH14_STORM_BITS)
    # Bit 15 of a 15-bit word; a channel mirrors it into bit 16, which is the
    # bit the scaler tests before letting TIME6 count.
    arm_bit.addr = a.here();      a.word(0o40000)
    time6_preset.addr = a.here(); a.word(0o7777)
    zero.addr = a.here();         a.word(0)

    measures = [
        ("LOOP_QUIET", sa1, sb1, None),
        ("LOOP_WITH_TIME6", sa2, sb2, None),
        ("LOOP_WITH_STORM", sa3, sb3, None),
    ]
    return a, measures


def main() -> int:
    a, measures = build()
    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    a.write_rope(str(OUT_BIN))

    lines = [
        "# Generated by tools/probes/gen_counter_probe.py — do not edit.",
        "# Counters are deliberately LEFT ENABLED here: the interference is the",
        "# measurement. The exact figure has no closed form (the loop's own",
        "# lengthening changes how many 1.6 kHz ticks land inside it), so it is",
        "# pinned by the golden and only the invariants are asserted.",
        "# name  open-sentinel  close-sentinel  expected-window-MCTs ('?' = record only)",
    ]
    for name, sa, sb, expect in measures:
        window = "?" if expect is None else str(expect)
        lines.append(f"measure {name} {sa:04o} {sb:04o} {window}")
    lines.append("# A stolen cycle is a whole MCT, and stealing cannot be free.")
    lines.append("relation LOOP_QUIET LOOP_WITH_TIME6 b_longer_by_whole_mcts")
    lines.append("# And it scales: seven counters cost more than one.")
    lines.append("relation LOOP_WITH_TIME6 LOOP_WITH_STORM b_longer_by_whole_mcts")
    lines.append(f"dump {TIME6:o}:1")
    OUT_META.write_text("\n".join(lines) + "\n")

    print(f"wrote {OUT_BIN.relative_to(ROOT)} and {OUT_META.relative_to(ROOT)}: "
          f"{a.here() - 0o4000} words")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
