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
        self.expects: list[tuple[str, int, int, str]] = []  # (name, addr, value, op)
        self.dumps: list[str] = []
        self.stops: list[int] = []
        self.mct = 100000

        for line in meta_path.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            head, *rest = line.split()
            if head == "flags":
                if any(f in ("--mct", "--timepulses", "--sentinel") for f in rest):
                    raise SystemExit(
                        f"{meta_path}: use the 'mct' and 'stop_at' directives, not "
                        f"raw flags — --mct accumulates rather than replacing"
                    )
                self.flags += rest
            elif head == "mct":
                self.mct = int(rest[0])
            elif head == "stop_at":
                self.stops.append(int(rest[0], 8))
            elif head == "measure":
                name, sa, sb, expect = rest
                self.measures.append(
                    (name, int(sa, 8), int(sb, 8), None if expect == "?" else int(expect))
                )
            elif head == "relation":
                lhs, rhs, kind = rest
                self.relations.append((lhs, rhs, kind))
            elif head in ("expect_mem", "expect_mem_min", "expect_mem_max"):
                name, addr, value = rest
                op = {"expect_mem": "==",
                      "expect_mem_min": ">=",
                      "expect_mem_max": "<="}[head]
                self.expects.append((name, int(addr, 8), int(value, 8), op))
                self.dumps.append(f"{int(addr, 8):o}:1")
            elif head == "dump":
                self.dumps += rest
            else:
                raise SystemExit(f"{meta_path}: unknown directive {head!r}")

    def sentinels(self) -> list[int]:
        out: list[int] = []
        for _, sa, sb, _ in self.measures:
            out += [sa, sb]
        return out + self.stops

    def command(self, exe: str) -> list[str]:
        # `mct` is only an upper bound: the run stops as soon as every sentinel
        # has fired. That matters for more than speed. The AGC has no halt, so a
        # finished probe parks in a branch-to-itself — and a pure transfer-of-
        # control loop trips the TC TRAP alarm after about 10 ms, restarting the
        # machine and running the whole probe again on top of its own results.
        # Every probe therefore needs either sentinels or a `stop_at`.
        cmd = [exe, "--rope", str(self.bin), "--mct", str(self.mct)]
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


def parse_memory(output: str) -> dict[int, int]:
    """E <octal addr> <octal value>, from --dump-mem."""
    mem: dict[int, int] = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "E":
            mem[int(parts[1], 8)] = int(parts[2], 8)
    return mem


def check_expectations(probe: Probe, output: str, report: bool) -> list[str]:
    """Assertions on what the machine computed, as opposed to how long it took.

    `expect_mem_min` exists for claims of the form "at least this much happened
    before X" — the interrupt-refusal probe needs it, because the exact number
    of instructions that ran before the interrupt was finally taken depends on
    scaler phase, while the *claim* is only that it was refused for the whole
    time the accumulator held overflow.
    """
    failures: list[str] = []
    mem = parse_memory(output)
    for name, addr, want, op in probe.expects:
        if addr not in mem:
            failures.append(f"{probe.name}/{name}: cell {addr:04o} was not dumped")
            continue
        got = mem[addr]
        ok = {"==": got == want, ">=": got >= want, "<=": got <= want}[op]
        if not ok:
            failures.append(
                f"{probe.name}/{name}: cell {addr:04o} is {got:06o}, expected {op} {want:06o}"
            )
        elif report:
            print(f"  {name:<24} {addr:04o} = {got:06o}   ({op} {want:06o})")
    return failures


def check_relations(probe: Probe, windows: dict[str, int], report: bool) -> list[str]:
    """Invariants between two measurements, for effects with no closed form.

    `b_longer_by_whole_mcts` asserts that the second window exceeds the first by
    a positive whole number of Memory Cycle Times. Everything the AGC's sequence
    generator does costs whole MCTs — an involuntary counter sequence runs T1 to
    T12 like any other, and a branch not taken buys a whole extra STD2 — so a
    fractional difference means something is being spliced in at the wrong
    granularity, and a zero difference means the effect is not being modelled.
    """
    failures: list[str] = []
    for lhs, rhs, kind in probe.relations:
        if lhs not in windows or rhs not in windows:
            failures.append(f"{probe.name}: relation names an unknown measurement")
            continue
        delta = windows[rhs] - windows[lhs]
        if kind == "b_longer_by_whole_mcts":
            if delta <= 0:
                failures.append(
                    f"{probe.name}: {rhs} ({windows[rhs]} pulses) is not longer than "
                    f"{lhs} ({windows[lhs]} pulses) — the effect costs nothing"
                )
            elif delta % MCT != 0:
                failures.append(
                    f"{probe.name}: {rhs} - {lhs} is {delta} pulses, not a whole "
                    f"number of MCTs"
                )
            elif report:
                print(f"  {lhs} -> {rhs}: +{delta} pulses "
                      f"({delta // MCT} whole MCT{'s' if delta // MCT != 1 else ''})")
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
    return (failures
            + check_relations(probe, windows, report)
            + check_expectations(probe, output, report))


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

    for meta in metas:
        if not Probe(meta).sentinels():
            print(f"{meta.name}: no sentinels and no stop_at — the probe would park "
                  f"in a TC loop and restart itself on the TC TRAP alarm",
                  file=sys.stderr)
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
