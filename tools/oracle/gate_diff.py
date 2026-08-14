#!/usr/bin/env python3
"""Diff our committed pulse tables against the gate-level cross-point generator.

`gate_crosspoint.py` reads a subinstruction's control-pulse timeline out of the
netlists in `ext/agc_simulation`, which are generated from the original MIT
logic flow diagrams.  This walks every subinstruction that can be addressed that
way, every branch-register value and all twelve timing pulses, and compares the
result against `src/core/cpu/subinst_tables.c`.

That is a third, independent check on the tables: the memo is the primary
source, AGCPlusPlus is the corrected reading of it, and the gates decide when
those two disagree.  It found FINDINGS #11 — the MP3A term on the carry gate —
and confirmed FINDINGS #9.

    tools/oracle/gate_diff.py            # report and exit non-zero on a diff
    tools/oracle/gate_diff.py --verbose  # print the timelines as well

Exit status is 0 when every row agrees or is explained below.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import gate_crosspoint as gx  # noqa: E402

TABLES = gx.REPO / "src" / "core" / "cpu" / "subinst_tables.c"

#: The memo defines four pulses as shorthand for others.  The gates have no
#: signal named ZAP or ZIP; they assert what those stand for, so the comparison
#: expands ours the same way.  ZIP's read/write/carry choice depends on L15,L2,L1
#: at run time, which a static cross-point reading cannot know, so those are
#: compared out on any row carrying a ZIP.
IMPLIED = {"ZAP": {"RU"}, "ZIP": {"A2X", "L2GD"}}
ZIP_RUNTIME = {"WY", "WYD", "RB", "RC", "CI", "MCRO"}

#: Pulses that exist in our tables but are not cross-point outputs, so the four
#: modules loaded here cannot show them.  Each is accounted for elsewhere.
NOT_IN_THE_CROSS_POINT = {
    "G2LS",  # memo marks it: implied by ZAP, formed in the service gates (A7)
    "WALS",  # likewise
    "WSQ",  # likewise
    "STAGE",  # the divide grey-code advance, in stage_branch's own counter
    "TSGN2",  # formed in the parity/S register module (A12)
    "CLRIIP",  # RESUME dropping IIP: rupt_service (A15), FINDINGS #16
}

#: Sequences the gates can address but our tables do not carry.  TCSAJ3
#: ("transfer control to specified address jam") is driven by the Computer Test
#: Set, not by anything in flight, so ext/agcplusplus never implemented it and
#: neither have we.  Named here so its absence stays a decision rather than a
#: hole; it is a tail in docs/COMPLETION_PLAN.md.
NOT_MODELLED = {"TCSAJ3"}


def parse_tables() -> dict[str, list[tuple[int, int, int, set[str]]]]:
    src = TABLES.read_text()
    out: dict[str, list[tuple[int, int, int, set[str]]]] = {}
    for block in re.finditer(
        r"static const agc_pulse_row (\w+)_rows\[\] = \{(.*?)\n\};", src, re.S
    ):
        rows: list[tuple[int, int, int, set[str]]] = []
        for row in re.finditer(
            r"\{\s*(\d+),\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*\{([^}]*)\}", block.group(2)
        ):
            pulses = {
                p.strip().removeprefix("AGC_P_") for p in row.group(4).split(",") if p.strip()
            }
            rows.append((int(row.group(1)), int(row.group(2), 16), int(row.group(3), 16), pulses))
        out[block.group(1).upper()] = rows
    return out


def ours_at(rows, timing_pulse: int, branch: int) -> set[str]:
    pulses: set[str] = set()
    for tp, mask, value, row in rows:
        if tp == timing_pulse and (branch & mask) == value:
            pulses |= row
    return pulses


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not (gx.SIM_ROOT / "modules" / "crosspoint_ii.v").exists():
        print("ext/agc_simulation is not initialised; nothing to diff against")
        return 0

    tables = parse_tables()
    bench = gx.CrossPointBench()
    disagreements = 0
    checked = 0

    for name in gx.SUBINSTRUCTIONS:
        if name in NOT_MODELLED:
            continue
        rows = tables.get(name)
        if rows is None:
            print(f"{name}: no {name.lower()}_rows in subinst_tables.c", file=sys.stderr)
            disagreements += 1
            continue
        for branch in range(4):
            timeline = bench.timeline(name, branch)
            for tp in range(1, 13):
                checked += 1
                mine = ours_at(rows, tp, branch)
                has_zip = "ZIP" in mine
                for pulse in list(mine):
                    mine |= IMPLIED.get(pulse, set())
                theirs = {alias for p in timeline[tp] for alias in p.split("/")}
                extra, missing = theirs - mine, mine - theirs
                if has_zip:
                    extra -= ZIP_RUNTIME
                    missing -= ZIP_RUNTIME
                missing -= NOT_IN_THE_CROSS_POINT
                if extra or missing:
                    disagreements += 1
                    print(
                        f"{name} BR={branch:02b} T{tp}: "
                        f"gates assert {sorted(extra)}, we assert {sorted(missing)}"
                    )
                elif args.verbose and theirs:
                    print(f"{name} BR={branch:02b} T{tp:2d}: {' '.join(sorted(theirs))}")

    print(
        f"{checked} rows checked across "
        f"{len(gx.SUBINSTRUCTIONS) - len(NOT_MODELLED)} subinstructions: "
        f"{disagreements} disagree"
    )
    return 1 if disagreements else 0


if __name__ == "__main__":
    raise SystemExit(main())
