#!/usr/bin/env python3
"""Differential-test the instruction set against the runnable oracle.

For each instruction, over a sweep of operand values chosen to include the
awkward ones, this builds a tiny rope, runs it on both emulators for the same
number of timing pulses, and compares the full 16-bit registers and the erasable
cells the instruction touched.

Why registers and not just memory: bit 16 is the overflow bit, and anything
routed through erasable memory on its way out loses it. An instruction that
leaves the wrong overflow state would look correct in a memory dump.

This exists because divide was wrong for the project's entire life and nothing
noticed (FINDINGS #40). Unit tests passed, all 26 instruction timings matched
AGC4 Memo #9 — the divide took exactly the right number of pulses while
computing the wrong number — and five flight ropes booted without an alarm.
Timing correctness and result correctness are independent properties, and only
one of them was being checked.

The oracle is a model, not hardware. Where the two disagree, the answer is not
automatically "we are wrong" — but it is always a question worth resolving, and
it should be resolved in FINDINGS rather than by adjusting an expectation.

Usage:
    tools/oracle/differential.py --ours build/.../agc_headless --oracle build/oracle
    ... --instr DV          restrict to one instruction
    ... --verbose           print every case, not just the failures
"""
from __future__ import annotations

import argparse
import atexit
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools/probes"))
from asm import Asm, SCRATCH_START  # noqa: E402

# Per-process, because two invocations sharing one rope file will silently
# report each other's results as mismatches. That happened once: a background
# full sweep overlapping another run produced 403 phantom failures, including
# "DCS 112/256", which passed 256/256 the moment it was run on its own.
TMP = ROOT / f"build/differential-{os.getpid()}.bin"

# Operand values worth sweeping. Ones' complement has two zeroes and an
# asymmetric range, and most arithmetic bugs live at those edges rather than in
# the middle.
VALUES = [
    0o00000,  # +0
    0o00001,  # +1
    0o37777,  # largest positive
    0o77777,  # -0, the one a naive zero test misses
    0o77776,  # -1
    0o40000,  # largest negative
    0o12345,  # an arbitrary positive
    0o52525,  # an arbitrary negative, alternating bits
]

MEM0 = SCRATCH_START + 0  # 0100  the operand most instructions address
MEM1 = SCRATCH_START + 1  # 0101  the second word of a double-precision pair
SPARE = SCRATCH_START + 2 # 0102


class Case:
    """One instruction, set up and executed once."""

    def __init__(self, name, emit, *, extended=False, dp=False,
                 dumps=(MEM0,), ticks=600):
        self.name = name
        self.emit = emit
        self.extended = extended
        self.dp = dp
        self.dumps = dumps
        self.ticks = ticks


# Note the operand addresses: the double-precision instructions name the *less
# significant* word and reach the more significant one at address-1, so they are
# given MEM1 and touch MEM0 as well.
CASES = [
    Case("CA",    lambda a: a.ca(MEM0)),
    Case("CS",    lambda a: a.cs(MEM0)),
    Case("AD",    lambda a: a.ad(MEM0)),
    Case("MASK",  lambda a: a.mask(MEM0)),
    Case("TS",    lambda a: a.ts(MEM0)),
    Case("XCH",   lambda a: a.xch(MEM0)),
    Case("LXCH",  lambda a: a.lxch(MEM0)),
    Case("INCR",  lambda a: a.incr(MEM0)),
    Case("ADS",   lambda a: a.ads(MEM0)),
    Case("CCS",   lambda a: a.ccs(MEM0)),
    Case("SU",    lambda a: (a.extend(), a.su(MEM0)), extended=True),
    Case("MSU",   lambda a: (a.extend(), a.msu(MEM0)), extended=True),
    Case("QXCH",  lambda a: (a.extend(), a.qxch(MEM0)), extended=True),
    Case("AUG",   lambda a: (a.extend(), a.aug(MEM0)), extended=True),
    Case("DIM",   lambda a: (a.extend(), a.dim(MEM0)), extended=True),
    Case("MP",    lambda a: (a.extend(), a.mp(MEM0)), extended=True),
    Case("DV",    lambda a: (a.extend(), a.dv(MEM0)), extended=True),
    Case("DAS",   lambda a: a.das(MEM1), dp=True, dumps=(MEM0, MEM1)),
    Case("DXCH",  lambda a: a.dxch(MEM1), dp=True, dumps=(MEM0, MEM1)),
    Case("DCA",   lambda a: (a.extend(), a.dca(MEM1)), extended=True, dp=True,
         dumps=(MEM0, MEM1)),
    Case("DCS",   lambda a: (a.extend(), a.dcs(MEM1)), extended=True, dp=True,
         dumps=(MEM0, MEM1)),
]


def build_rope(case: Case, a_val: int, l_val: int, m0: int, m1: int) -> None:
    a = Asm()
    ka = a.label("ka")
    kl = a.label("kl")
    km0 = a.label("km0")
    km1 = a.label("km1")

    a.ca(km0); a.ts(MEM0)
    a.ca(km1); a.ts(MEM1)
    a.ca(kl);  a.ts(1)     # L
    a.ca(ka)               # A last, so nothing disturbs it

    case.emit(a)

    # CCS and TS can skip; land the skip somewhere harmless. Park without a
    # transfer-of-control loop is impossible on this machine, but the run is far
    # too short for TC TRAP to notice.
    for _ in range(4):
        a.noop()
    park = a.label("park")
    a.tcf(park)

    ka.addr = a.here();  a.word(a_val)
    kl.addr = a.here();  a.word(l_val)
    km0.addr = a.here(); a.word(m0)
    km1.addr = a.here(); a.word(m1)
    a.write_rope(str(TMP))


REG_RE = re.compile(r"A=(\S+) L=(\S+) Q=(\S+) Z=(\S+)")


def run_ours(exe: str, case: Case) -> dict:
    out = subprocess.run(
        [exe, "--rope", str(TMP), "--timepulses", str(case.ticks),
         "--ignore-counters", "--ignore-interrupts", "--ignore-alarms",
         "--dump-state"] + [x for d in case.dumps
                            for x in ("--dump-mem", f"{d:o}:1")],
        capture_output=True, text=True)
    return parse(out.stdout, ours=True)


def run_oracle(exe: str, case: Case) -> dict:
    out = subprocess.run(
        [exe, str(TMP), str(case.ticks)] + [f"{d:o}" for d in case.dumps],
        capture_output=True, text=True)
    return parse(out.stdout, ours=False)


def parse(text: str, *, ours: bool) -> dict:
    state = {}
    for line in text.splitlines():
        parts = line.split()
        if parts and parts[0] == "E":
            # An erasable word is 15 bits. We dump the raw cell; the oracle
            # reads it through the destructive read, which duplicates the sign
            # into bit 16 on the way out. Same word, different presentation —
            # compare the 15 bits that are actually stored.
            state[f"E{int(parts[1], 8):04o}"] = f"{int(parts[2], 8) & 0o77777:06o}"
        elif ours and " A=" in line:
            m = REG_RE.search(line)
            if m:
                state.update(A=m.group(1), L=m.group(2), Q=m.group(3), Z=m.group(4))
        elif not ours and parts and parts[0] == "REG":
            for f in parts[1:]:
                k, v = f.split("=")
                if k in ("A", "L", "Q", "Z"):
                    state[k] = v
    return state


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--oracle", required=True)
    ap.add_argument("--instr", help="restrict to one instruction")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--quick", action="store_true",
                    help="a small sweep for routine use; the full one takes ~2 minutes")
    args = ap.parse_args()

    if not Path(args.oracle).exists():
        build = ROOT / "tools/oracle/build_oracle.sh"
        if not (ROOT / "ext/agcplusplus/src/block2/cpu.cpp").exists():
            print("ext/agcplusplus is not initialised; skipping the differential "
                  "test. Run: git submodule update --init ext/agcplusplus")
            return 0
        print(f"building the oracle ...")
        subprocess.run([str(build), args.oracle], check=True)

    TMP.parent.mkdir(parents=True, exist_ok=True)
    atexit.register(lambda: TMP.unlink(missing_ok=True))
    cases = [c for c in CASES if not args.instr or c.name == args.instr]
    if not cases:
        print(f"no such instruction: {args.instr}", file=sys.stderr)
        return 1

    # The quick sweep keeps the two zeroes and the largest positive, which is
    # where the ones'-complement edge cases live; the full sweep adds the
    # negative extremes and two arbitrary bit patterns.
    values = [0o00000, 0o77777, 0o37777] if args.quick else VALUES

    total = mismatched = 0
    failures: list[str] = []

    for case in cases:
        n = bad = 0
        for a_val in values:
            for m0 in values:
                # L and the second memory word only matter to the
                # double-precision and multiply/divide instructions; hold them
                # fixed elsewhere to keep the sweep to a sensible size.
                l_vals = values if (case.dp or case.name in ("MP", "DV")) else [0o12345]
                for l_val in l_vals[:4]:
                    m1 = m0 if not case.dp else (m0 ^ 0o01234) & 0o77777
                    build_rope(case, a_val, l_val, m0, m1)
                    ours = run_ours(args.ours, case)
                    orac = run_oracle(args.oracle, case)
                    n += 1
                    total += 1
                    if ours != orac:
                        bad += 1
                        mismatched += 1
                        if len(failures) < 20:
                            diff = {k: (ours.get(k), orac.get(k))
                                    for k in set(ours) | set(orac)
                                    if ours.get(k) != orac.get(k)}
                            failures.append(
                                f"{case.name}: A={a_val:06o} L={l_val:06o} "
                                f"M={m0:06o} -> " +
                                ", ".join(f"{k} ours={v[0]} oracle={v[1]}"
                                          for k, v in sorted(diff.items())))
        status = "ok" if bad == 0 else f"{bad}/{n} MISMATCH"
        if args.verbose or bad:
            print(f"  {case.name:<6} {n:4d} cases  {status}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
    print(f"\n{total - mismatched}/{total} cases agree with the oracle")
    return 1 if mismatched else 0


if __name__ == "__main__":
    raise SystemExit(main())
