#!/usr/bin/env python3
"""Generate the instruction-timing probe.

The probe measures how long each instruction actually takes, in emulated timing
pulses, by bracketing it between two sentinel stores:

        <setup>              ; outside the measured window
        CA  ONE
        TS  Sa               ; window opens here
        <instruction under test>
        CA  ONE
        TS  Sb               ; window closes here

Sa and Sb are stored by identical `TS` instructions, so the partial MCTs at
either end cancel exactly and the window is

        cost(instruction) + cost(CA) + cost(TS)  =  cost + 4 MCTs.

Each instruction's documented cost is asserted by tools/regress.py, which turns
AGC4 Memo #9's sequence tables into an executable oracle: an instruction whose
subinstruction chain is wrong by one MCT fails here even if it computes the
right answer.

Divide was the one measurement taken before it was asserted: DVST licenses a
divide sub-sequence to end at T3 rather than T12, so a divide is assembled from
unequal segments and there was no reason to assume the total came out on a whole
Memory Cycle Time boundary. It does — exactly 72 timing pulses, the documented
6 MCTs. Recorded in tools/oracle/FINDINGS.md and now asserted like the rest.

Counters are disabled for this probe (tools/regress.py passes
--ignore-counters). A counter request steals a whole MCT from the program, so
with the scaler running some measurements would come out one MCT long depending
on where in the 100 Hz tick they happened to land. That interference is real
and worth measuring — but deliberately, in its own probe, not as noise on top
of every instruction.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from asm import Asm, SCRATCH_START  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent
OUT_BIN = ROOT / "tests/probes/timing.bin"
OUT_META = ROOT / "tests/probes/timing.meta"

# Erasable scratch, clear of the central registers (0-7), the editing registers
# (010-023), the counter cells (024-060) and the night watchman's cell (067).
ONE_E = SCRATCH_START + 0     # 0100
OPA = SCRATCH_START + 1       # 0101  general operand
ZERO_E = SCRATCH_START + 2    # 0102  a +0, for INDEX
DIVISOR = SCRATCH_START + 3   # 0103  DV addresses erasable only
DP_HI = SCRATCH_START + 4     # 0104  double-precision pair, more significant
DP_LO = SCRATCH_START + 5     # 0105  ... less significant
SENTINEL_BASE = SCRATCH_START + 0o20  # 0120 upward


def m_ca(a, k):     a.ca(k["one"])
def m_cs(a, k):     a.cs(k["one"])
def m_ad(a, k):     a.ad(k["one"])
def m_mask(a, k):   a.mask(k["one"])
def m_ts(a, k):     a.ts(OPA)
def m_xch(a, k):    a.xch(OPA)
def m_lxch(a, k):   a.lxch(OPA)
def m_incr(a, k):   a.incr(OPA)
def m_ads(a, k):    a.ads(OPA)
def m_tc(a, k):     a.tc(a.here() + 1)
def m_tcf(a, k):    a.tcf(a.here() + 1)
def m_inhint(a, k): a.inhint()
def m_relint(a, k): a.relint()
def m_das(a, k):    a.das(DP_LO)
def m_dxch(a, k):   a.dxch(DP_LO)
def m_su(a, k):     a.extend(); a.su(OPA)
def m_msu(a, k):    a.extend(); a.msu(OPA)
def m_qxch(a, k):   a.extend(); a.qxch(OPA)
def m_aug(a, k):    a.extend(); a.aug(OPA)
def m_dim(a, k):    a.extend(); a.dim(OPA)
def m_dca(a, k):    a.extend(); a.dca(k["dp_lo"])
def m_dcs(a, k):    a.extend(); a.dcs(k["dp_lo"])
def m_mp(a, k):     a.extend(); a.mp(k["small"])
def m_dv(a, k):     a.extend(); a.dv(DIVISOR)


def m_index(a, k):
    """INDEX adds an erasable word to the instruction that follows it. Index by
    +0 so nothing is modified — and let the window's own closing `CA ONE` be
    the indexed instruction, so the window stays cost(INDEX) + 4."""
    a.index(ZERO_E)


def m_ccs(a, k):
    """CCS skips into one of four branch words, so all four must be real
    instructions that rejoin: a zero word there would decode as TC 0 and run
    away into the accumulator. The window therefore covers CCS *and* the
    branch's transfer, which is why this one is named for both."""
    a.ccs(OPA)
    rejoin = a.here() + 4
    for _ in range(4):
        a.tcf(rejoin)


def setup_operand(a, k):
    a.ca(k["small"])
    a.ts(OPA)


def setup_dv(a, k):
    """DV divides the double word (A, L) by an erasable divisor. A is 1 when the
    window opens (the `CA ONE` that precedes the sentinel store), so the
    quotient stays in range as long as the divisor is comfortably larger."""
    a.ca(k["one"])
    a.ts(1)              # L = 1
    a.ca(k["big"])
    a.ts(DIVISOR)


# (name, expected instruction cost in MCTs or None to record-only, setup, body)
#
# The expected figures are the documented Block II instruction times: one MCT
# per subinstruction, plus the STD2 that fetches the next instruction — except
# for instructions that fetch for themselves (TC and TCF assert NISQ and RAD in
# their own final subinstruction). Extracodes carry the extra MCT of the EXTEND
# pseudo-code in front of them.
MEASUREMENTS = [
    ("CA",           2,    None,          m_ca),
    ("CS",           2,    None,          m_cs),
    ("AD",           2,    None,          m_ad),
    ("MASK",         2,    None,          m_mask),
    ("TS",           2,    None,          m_ts),
    ("XCH",          2,    None,          m_xch),
    ("LXCH",         2,    None,          m_lxch),
    ("INCR",         2,    None,          m_incr),
    ("ADS",          2,    None,          m_ads),
    ("TC",           1,    None,          m_tc),
    ("TCF",          1,    None,          m_tcf),
    ("INHINT",       1,    None,          m_inhint),
    ("RELINT",       1,    None,          m_relint),
    ("INDEX",        2,    None,          m_index),
    ("CCS_PLUS_TCF", 3,    setup_operand, m_ccs),
    ("DAS",          3,    None,          m_das),
    ("DXCH",         3,    None,          m_dxch),
    ("SU",           3,    setup_operand, m_su),
    ("MSU",          3,    setup_operand, m_msu),
    ("QXCH",         3,    setup_operand, m_qxch),
    ("AUG",          3,    setup_operand, m_aug),
    ("DIM",          3,    setup_operand, m_dim),
    ("DCA",          4,    None,          m_dca),
    ("DCS",          4,    None,          m_dcs),
    # MP is 3 MCTs and DV is 6, both plus the EXTEND in front. DV was measured
    # before it was asserted: DVST lets a divide sub-sequence end at T3 instead
    # of T12, so a divide is built from unequal segments and there was no reason
    # to assume the total landed on a whole MCT. It does — exactly 72 timing
    # pulses. See tools/oracle/FINDINGS.md #25.
    ("MP",           4,    None,          m_mp),
    ("DV",           7,    setup_dv,      m_dv),
]


def build():
    a = Asm()

    # Constants live past the code; reserve their labels before laying it out.
    one = a.label("one")
    small = a.label("small")
    big = a.label("big")
    dp_hi_k = a.label("dp_hi_k")
    dp_lo_k = a.label("dp_lo_k")
    zero = a.label("zero")
    k = {"one": one, "small": small, "big": big,
         "dp_hi": dp_hi_k, "dp_lo": dp_lo_k, "zero": zero}

    # --- preamble ------------------------------------------------------------
    a.ca(one);   a.ts(ONE_E)
    a.ca(small); a.ts(OPA)
    a.ca(zero);  a.ts(ZERO_E)
    a.ca(big);   a.ts(DIVISOR)
    a.ca(small); a.ts(DP_HI)
    a.ca(one);   a.ts(DP_LO)

    # --- measurements --------------------------------------------------------
    sentinels = []
    for i, (name, expected, setup, body) in enumerate(MEASUREMENTS):
        sa = SENTINEL_BASE + 2 * i
        sb = SENTINEL_BASE + 2 * i + 1
        if setup is not None:
            setup(a, k)
        a.ca(one)
        a.ts(sa)
        body(a, k)
        a.ca(one)
        a.ts(sb)
        sentinels.append((name, sa, sb, expected))

    park = a.label("park")
    a.tcf(park)

    # --- constants -----------------------------------------------------------
    # DCA and DCS address the *less significant* word of a pair and reach the
    # more significant one at address-1, so both must be woven or the fetch
    # fails parity.
    one.addr = a.here();     a.word(1)
    small.addr = a.here();   a.word(0o10)
    big.addr = a.here();     a.word(0o1000)
    dp_hi_k.addr = a.here(); a.word(0o20)
    dp_lo_k.addr = a.here(); a.word(0o21)
    zero.addr = a.here();    a.word(0)

    return a, sentinels


def main() -> int:
    a, sentinels = build()
    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    a.write_rope(str(OUT_BIN))

    lines = [
        "# Generated by tools/probes/gen_timing_probe.py — do not edit.",
        "# Each measurement brackets one instruction between two sentinel stores;",
        "# the window is cost(instruction) + 4 MCTs (one CA and one TS).",
        "flags --ignore-counters",
        "# name  open-sentinel  close-sentinel  expected-window-MCTs ('?' = record only)",
    ]
    for name, sa, sb, expected in sentinels:
        window = "?" if expected is None else str(expected + 4)
        lines.append(f"measure {name} {sa:04o} {sb:04o} {window}")
    OUT_META.write_text("\n".join(lines) + "\n")

    print(f"wrote {OUT_BIN.relative_to(ROOT)} and {OUT_META.relative_to(ROOT)}: "
          f"{len(sentinels)} measurements, {a.here() - 0o4000} words")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
