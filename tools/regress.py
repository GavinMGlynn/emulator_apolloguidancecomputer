#!/usr/bin/env python3
"""Run every probe and check it against its golden — and against the memo.

Two independent checks per probe:

1. **The golden.** The probe's full output is diffed against a checked-in file.
   Because the output is emulated timing-pulse counts and emulated memory, not
   a measurement of anything on this host, the golden is bit-identical on every
   platform and every build type. A host where it differs has a real bug —
   undefined behaviour, a width assumption — and CI runs this everywhere.

2. **The oracle.** Where a probe's `.meta` states an expected value taken from
   AGC4 Memo #9, the measurement is checked against it. This is what makes the
   suite more than a change detector: an instruction whose subinstruction chain
   is one MCT wrong fails here on the day it is written, even if it happens to
   compute the right answer.

Usage:
    tools/regress.py --exe <agc_headless>            # check
    tools/regress.py --exe <agc_headless> --bless    # (re)write the goldens
    tools/regress.py --exe <agc_headless> --report   # show every measurement
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROBE_DIR = ROOT / "tests/probes"
GOLDEN_DIR = ROOT / "tests/goldens"
MCT = 12  # timing pulses


class Probe:
    def __init__(self, meta_path: Path):
        self.name = meta_path.stem
        self.bin = meta_path.with_suffix(".bin")
        self.golden = GOLDEN_DIR / f"{self.name}.golden"
        self.flags: list[str] = []
        self.measures: list[tuple[str, int, int, int | None]] = []
        self.relations: list[tuple[str, str, str]] = []
        self.dumps: list[str] = []

        for line in meta_path.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            head, *rest = line.split()
            if head == "flags":
                self.flags += rest
            elif head == "measure":
                name, sa, sb, expect = rest
                self.measures.append(
                    (name, int(sa, 8), int(sb, 8), None if expect == "?" else int(expect))
                )
            elif head == "relation":
                lhs, rhs, kind = rest
                self.relations.append((lhs, rhs, kind))
            elif head == "dump":
                self.dumps += rest
            else:
                raise SystemExit(f"{meta_path}: unknown directive {head!r}")

    def sentinels(self) -> list[int]:
        out: list[int] = []
        for _, sa, sb, _ in self.measures:
            out += [sa, sb]
        return out

    def command(self, exe: str) -> list[str]:
        cmd = [exe, "--rope", str(self.bin), "--mct", "100000"]
        cmd += self.flags
        for addr in self.sentinels():
            cmd += ["--sentinel", f"{addr:o}"]
        for spec in self.dumps:
            cmd += ["--dump-mem", spec]
        return cmd

    def run(self, exe: str) -> str:
        proc = subprocess.run(self.command(exe), capture_output=True, text=True)
        if proc.returncode != 0:
            raise SystemExit(f"{self.name}: emulator exited {proc.returncode}\n{proc.stderr}")
        return proc.stdout


def parse_sentinels(output: str) -> dict[int, int | None]:
    """SENT <octal addr> <mct> <timepulses>, or SENT <addr> never."""
    seen: dict[int, int | None] = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] == "SENT":
            addr = int(parts[1], 8)
            seen[addr] = None if parts[2] == "never" else int(parts[3])
    return seen


def check_relations(probe: Probe, windows: dict[str, int], report: bool) -> list[str]:
    """Invariants between two measurements, for effects with no closed form."""
    failures: list[str] = []
    for lhs, rhs, kind in probe.relations:
        if lhs not in windows or rhs not in windows:
            failures.append(f"{probe.name}: relation names an unknown measurement")
            continue
        delta = windows[rhs] - windows[lhs]
        if kind == "costs_whole_mcts":
            if delta <= 0:
                failures.append(
                    f"{probe.name}: {rhs} ({windows[rhs]} pulses) is not longer than "
                    f"{lhs} ({windows[lhs]} pulses) — counter servicing appears free"
                )
            elif delta % MCT != 0:
                failures.append(
                    f"{probe.name}: {rhs} - {lhs} is {delta} pulses, not a whole "
                    f"number of MCTs — a stolen cycle must run T1 to T12"
                )
            elif report:
                print(f"  {lhs} -> {rhs}: {delta} pulses lost to counter servicing "
                      f"({delta // MCT} whole MCTs stolen)")
        else:
            failures.append(f"{probe.name}: unknown relation {kind!r}")
    return failures


def check_oracle(probe: Probe, output: str, report: bool) -> list[str]:
    failures: list[str] = []
    fired = parse_sentinels(output)
    windows: dict[str, int] = {}
    for name, sa, sb, expect in probe.measures:
        open_tp, close_tp = fired.get(sa), fired.get(sb)
        if open_tp is None or close_tp is None:
            failures.append(f"{probe.name}/{name}: sentinel never fired "
                            f"(open={open_tp}, close={close_tp})")
            continue
        pulses = close_tp - open_tp
        windows[name] = pulses
        mct, rem = divmod(pulses, MCT)
        shown = f"{mct} MCT" if rem == 0 else f"{mct} MCT + {rem} pulses"
        if expect is None:
            if report:
                print(f"  {name:<14} {pulses:5d} pulses  ({shown})   [record only]")
            continue
        if pulses != expect * MCT:
            failures.append(
                f"{probe.name}/{name}: window is {pulses} pulses ({shown}), "
                f"memo says {expect} MCT ({expect * MCT} pulses)"
            )
        elif report:
            print(f"  {name:<14} {pulses:5d} pulses  ({shown})   matches the memo")
    return failures + check_relations(probe, windows, report)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True, help="path to agc_headless")
    ap.add_argument("--bless", action="store_true", help="(re)write goldens")
    ap.add_argument("--report", action="store_true", help="print every measurement")
    args = ap.parse_args()

    metas = sorted(PROBE_DIR.glob("*.meta"))
    if not metas:
        print(f"no probes in {PROBE_DIR}", file=sys.stderr)
        return 1

    GOLDEN_DIR.mkdir(parents=True, exist_ok=True)
    failures: list[str] = []

    for meta in metas:
        probe = Probe(meta)
        if args.report:
            print(f"{probe.name}:")
        output = probe.run(args.exe)

        failures += check_oracle(probe, output, args.report)

        if args.bless:
            probe.golden.write_text(output)
            print(f"blessed {probe.golden.relative_to(ROOT)}")
        elif not probe.golden.exists():
            failures.append(f"{probe.name}: no golden — run with --bless")
        elif probe.golden.read_text() != output:
            failures.append(f"{probe.name}: output differs from "
                            f"{probe.golden.relative_to(ROOT)}")
            for want, got in zip(probe.golden.read_text().splitlines(),
                                 output.splitlines()):
                if want != got:
                    failures.append(f"    golden: {want}")
                    failures.append(f"    ours:   {got}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        print(f"\n{len(failures)} failure(s)", file=sys.stderr)
        return 1

    print(f"{len(metas)} probe(s) OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
