#!/usr/bin/env python3
"""Read control-pulse timelines straight out of the gate-level cross-point generator.

R-700 (Hall, *MIT's Role in Project Apollo* vol. III, p. 5-6) describes the
sequence generator as "the equivalent of a wired memory ... addressed by time
pulses, by instruction codes, by a memory cycle stage counter, and by tests of
priority activity.  The output ... is a sequence of control pulses that are
formed by a cross-point generator as a logic product of the appropriate time
pulses and instruction codes."

That wired memory is four modules of the Block II logic, and `ext/agc_simulation`
holds all four as gate netlists generated from the original MIT drawings:

    A3  sq_register    2005258   instruction decode (SQ register -> subinstruction)
    A4  stage_branch   2005262   stage counter, branch register, some pulses
    A5  crosspoint_nqi 2005261   CROSS POINT GENERATOR NQI
    A6  crosspoint_ii  2005263   CROSS POINT GENERATOR II

This script loads those four, holds the SQ register / stage counter / branch
register at a chosen subinstruction the way a bench technician would, walks
T1-T12, and prints which control pulses the gates assert.  That is the same
table AGC4 Memo #9 prints — read from the hardware instead of from the memo.

    tools/oracle/gate_crosspoint.py DAS1
    tools/oracle/gate_crosspoint.py MP3 --branch 1x
    tools/oracle/gate_crosspoint.py --list

Where the memo, AGCPlusPlus and the gates disagree, the gates decide; see
`tools/oracle/FINDINGS.md`.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import gate_sim  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
SIM_ROOT = REPO / "ext" / "agc_simulation"
MODULES = ["sq_register", "stage_branch", "crosspoint_nqi", "crosspoint_ii"]

#: The SQ register's own bit outputs inside module A3, most significant first.
#: Forcing these is how the bench loads an order code without running a rope.
SQ_BITS = (
    "__A03_1__SQR16",
    "__A03_1__SQR14",
    "__A03_1__SQR13",
    "__A03_1__SQR12",
    "__A03_1__SQR11",
)

#: subinstruction -> (extend, order code, quarter code, stage, decode line).
#: Every row is *checked at run time*: the named decode line must come up, so a
#: wrong guess here fails loudly rather than quietly probing the wrong column.
#: The encoding itself was read off the netlist, not assumed — sweeping the five
#: SQ bits and the stage counter reproduces the Block II order code exactly.
SUBINSTRUCTIONS: dict[str, tuple[int, int, int, int, str]] = {
    "TC0": (0, 0, 0, 0, "TC0"),
    "GOJ1": (0, 0, 0, 1, "GOJ1"),
    "TCSAJ3": (0, 0, 0, 3, "TCSAJ3"),
    "CCS0": (0, 1, 0, 0, "CCS0"),
    "TCF0": (0, 1, 1, 0, "TCF0"),
    "DAS0": (0, 2, 0, 0, "DAS0"),
    "DAS1": (0, 2, 0, 1, "DAS1"),
    "INCR0": (0, 2, 2, 0, "INCR0"),
    "ADS0": (0, 2, 3, 0, "ADS0"),
    "NDX0": (0, 5, 0, 0, "NDX0_n"),
    "RSM3": (0, 5, 0, 3, "RSM3"),
    "DXCH0": (0, 5, 1, 0, "DXCH0"),
    "TS0": (0, 5, 2, 0, "TS0"),
    "AD0": (0, 6, 0, 0, "AD0"),
    "MSK0": (0, 7, 0, 0, "MASK0"),
    "READ0": (1, 0, 0, 0, "READ0"),
    "RAND0": (1, 0, 1, 0, "RAND0"),
    "ROR0": (1, 0, 2, 0, "ROR0"),
    "RXOR0": (1, 0, 3, 0, "RXOR0"),
    "MSU0": (1, 2, 0, 0, "MSU0"),
    "QXCH0": (1, 2, 1, 0, "QXCH0_n"),
    "AUG0": (1, 2, 2, 0, "AUG0_n"),
    "DIM0": (1, 2, 3, 0, "DIM0_n"),
    "DCA0": (1, 3, 0, 0, "DCA0"),
    "DCS0": (1, 4, 0, 0, "DCS0"),
    "NDXX1": (1, 5, 0, 1, "NDXX1_n"),
    "SU0": (1, 6, 0, 0, "SU0"),
    "MP0": (1, 7, 0, 0, "MP0"),
    "MP1": (1, 7, 0, 1, "MP1"),
    "MP3": (1, 7, 0, 3, "MP3"),
}

#: memo mnemonic -> net that carries it.  A trailing `_n` marks the active-low
#: form; nets prefixed `__A0n_m__` are internal to a sheet and have no pin.
#:
#: The set is exactly the memo's control-pulse list, which is also our
#: `agc_pulse` enum, so a timeline diffs directly against `subinst_tables.c`.
#: Gate signals that are not control pulses are deliberately absent — TSUDO_n,
#: for one, is a subinstruction-level qualifier (`NOR(IC3, RSM3, MP3, IC16)`)
#: that stands for a whole MCT and would otherwise show up under all twelve
#: timing pulses of MP3.
PULSE_NETS: dict[str, str] = {
    "A2X": "A2X_n",
    "B15X": "B15X",
    "CI": "CI_n",
    "CLXC": "CLXC",
    "DVST": "DVST",
    "EXT": "EXT",
    "KRPT": "KRPT",
    "L16": "L16_n",
    "L2GD": "L2GD_n",
    "MONEX": "MONEX_n",
    "MOUT": "MOUT_n",
    "NISQ": "NISQ",
    "PONEX": "PONEX",
    "POUT": "POUT_n",
    "PTWOX": "PTWOX",
    "R15": "R15",
    "R1C": "R1C",
    "R6": "R6",
    "RA": "RA_n",
    "RAD": "RAD",
    "RB": "RB_n",
    "RB1": "RB1",
    "RB1F": "RB1F",
    "RB2": "RB2",
    "RBBK": "__A06_2__RDBANK",
    "RC": "RC_n",
    "RCH": "RCH_n",
    "RG": "RG_n",
    "RL": "RL_n",
    "RL10BB": "RL10BB",
    "RQ": "RQ_n",
    "RRPA": "RRPA",
    "RSC": "RSC_n",
    "RSCT": "RSCT",
    "RSTRT": "RSTRT",
    "RSTSTG": "RSTSTG",
    "RU": "RU_n",
    "RUS": "RUS_n",
    "RZ": "RZ_n",
    "ST1": "ST1",
    "ST2": "ST2",
    "TMZ": "TMZ_n",
    "TOV": "TOV_n",
    "TPZG": "TPZG_n",
    "TRSM": "TRSM",
    "TSGN": "TSGN_n",
    "TSGU": "TSGU_n",
    "U2BBK": "U2BBK",
    "WA": "WA_n",
    "WB": "WB_n",
    "WCH": "WCH_n",
    "WG": "WG_n",
    "WL": "WL_n",
    "WOVR": "WOVR_n",
    "WQ": "WQ_n",
    "WS": "WS_n",
    "WSC": "WSC_n",
    "WY": "WY_n",
    "WY12": "WY12_n",
    "WYD": "WYD_n",
    "WZ": "WZ_n",
    "Z15": "Z15_n",
    "Z16": "Z16_n",
    "ZAP": "ZAP_n",
    "ZIP": "__A06_1__ZIP",
    "ZOUT": "ZOUT_n",
    # PIFL and NEAC are latches, not cross-point outputs, so their *levels*
    # have no meaning in a zero-timing model.  Both are reported by the term
    # that sets them, which is what the memo's tables actually mark.
    # The memo's NEACON/NEACOF are not cross-point outputs at all: they are the
    # set and reset terms of the NEAC latch (A6 gates 40426/40427, drawing
    # 2005263).  MP0T10 sets it; TL15 — which is also the control pulse that
    # copies L15 into BR1 — clears it.  Reporting the terms rather than the
    # latch is both truer to the hardware and readable in a zero-timing model,
    # where an SR latch with neither term asserted has no defined level.
    "NEACON": "MP0T10",
    "PIFL(set)": "__A06_1__DVXP1",
    "TL15/NEACOF": "TL15",
}

STAGE_LINES = ("ST0_n", "ST1_n", "STD2", "ST3_n")

#: The divide grey counter (module A4) is a free-running latch chain: with no
#: divide in progress and no clock, a zero-timing evaluation leaves it in an
#: arbitrary state, and that state leaks into the cross-point matrix — it was
#: seen adding a WB to DAS1 T3 that no source prints.  Every subinstruction
#: here is a non-divide one, so its divide conditions are held quiet, which is
#: what the machine's own grey counter holds them at.  Probing the DV
#: sequences needs the counter driven stage by stage instead; that is a
#: separate job and this script does not pretend to do it.
DIVIDE_QUIET = {
    "DIV_n": 1,
    "DIVSTG": 0,
    "DV1": 0,
    "DV1_n": 1,
    "DV4": 0,
    "DV4_n": 1,
    "DV4B1B": 0,
    "DV1376": 0,
    "DV1376_n": 1,
    "DV376_n": 1,
    "DV3764": 0,
}


class CrossPointBench:
    """The four sequence-generator modules, held at one subinstruction."""

    def __init__(self) -> None:
        self.netlist = gate_sim.build(SIM_ROOT, MODULES)
        self.nets = self.netlist.nets
        self.quiescent = {
            net: (1 if net.endswith("_n") else 0)
            for net in self.netlist.undriven_nets()
            if not net.startswith("__nc_")
        }
        #: Learned on the first evaluation and reused, purely to stop the settle
        #: loop early: without it every row pays the full step budget waiting on
        #: latches that cannot converge with no clock (GNHNC's, in practice).
        self._ringing: frozenset[str] = frozenset()

    def _forces(self, name: str, branch: int) -> dict[str, int]:
        ext, order, qc, stage, _ = SUBINSTRUCTIONS[name]
        bits = ((order >> 2) & 1, (order >> 1) & 1, order & 1, (qc >> 1) & 1, qc & 1)
        forces = dict(self.quiescent)
        forces.update(zip(SQ_BITS, bits, strict=True))
        forces.update(SQEXT=ext, SQEXT_n=1 - ext, SQR10=0, SQR10_n=1)
        forces.update(ST0_n=1, ST1_n=1, STD2=0, ST3_n=1)
        line = STAGE_LINES[stage]
        forces[line] = 1 if line == "STD2" else 0
        br1, br2 = (branch >> 1) & 1, branch & 1
        forces.update(BR1=br1, BR1_n=1 - br1, BR2=br2, BR2_n=1 - br2)
        forces.update(DIVIDE_QUIET)
        return {net: value for net, value in forces.items() if net in self.nets}

    def pulses_at(self, name: str, timing_pulse: int, branch: int) -> set[str]:
        """The control pulses the gates assert at one timing pulse."""
        sim = gate_sim.Simulator(self.netlist)
        forces = self._forces(name, branch)
        for t in range(1, 13):
            on = t == timing_pulse
            forces[f"T{t:02d}"] = int(on)
            forces[f"T{t:02d}_n"] = int(not on)
        sim.drive(**{net: value for net, value in forces.items() if net in self.nets})
        unstable = sim.settle(known_ringing=self._ringing)
        self._ringing |= unstable

        decode = SUBINSTRUCTIONS[name][4]
        selected = sim.read([decode], unstable)[decode]
        if selected == decode.endswith("_n"):
            raise RuntimeError(f"{name}: decode line {decode} did not come up")

        wanted = [net for net in PULSE_NETS.values() if net in self.nets]
        values = sim.read(wanted, unstable)
        return {
            mnemonic
            for mnemonic, net in PULSE_NETS.items()
            if net in values and values[net] == int(not net.endswith("_n"))
        }

    def timeline(self, name: str, branch: int) -> dict[int, set[str]]:
        return {t: self.pulses_at(name, t, branch) for t in range(1, 13)}


BRANCHES = {"00": 0, "01": 1, "10": 2, "11": 3}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("subinstruction", nargs="?", help="e.g. DAS1, MP3")
    ap.add_argument(
        "--branch",
        default="all",
        help="branch register: 00, 01, 10, 11, x0/x1/0x/1x, or all (default)",
    )
    ap.add_argument("--list", action="store_true", help="list what can be probed")
    args = ap.parse_args()

    if args.list or not args.subinstruction:
        for name in SUBINSTRUCTIONS:
            print(name)
        return 0

    name = args.subinstruction.upper()
    if name not in SUBINSTRUCTIONS:
        ap.error(f"unknown subinstruction {name}; --list shows what is available")

    if args.branch == "all":
        branches = [0, 1, 2, 3]
    elif args.branch in BRANCHES:
        branches = [BRANCHES[args.branch]]
    else:
        pattern = args.branch
        branches = [
            b
            for b in range(4)
            for bits in [f"{b:02b}"]
            if all(p in ("x", bit) for p, bit in zip(pattern, bits, strict=True))
        ]
        if not branches:
            ap.error(f"unreadable branch pattern {args.branch!r}")

    bench = CrossPointBench()
    print(f"{name}  (gate level: {', '.join(MODULES)})")
    for branch in branches:
        print(f"\n  BR = {branch:02b}")
        for t, pulses in bench.timeline(name, branch).items():
            if pulses:
                print(f"   {t:2d}.  {'  '.join(sorted(pulses))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
