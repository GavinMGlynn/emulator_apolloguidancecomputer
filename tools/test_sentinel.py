#!/usr/bin/env python3
"""The sentinel forms, checked against a rope built to need both of them.

`--sentinel A` reports when a cell first becomes non-zero, which is the right
question for almost every probe: a cell starts as cleared erasable and the
program drops a marker in it. But it cannot ask the other question. A cell that
is *cleared* by the program lands on the value that means "nothing has happened
here yet", so the moment is invisible — and moments like that are real ones. A
counter reaching zero, a flag being taken down, a buffer being released.

`--sentinel A=V` asks for a value instead, and because it watches the edge into
V rather than the level at V, it can name zero without firing immediately
against erasable that was already zero.

The rope below does exactly the sequence that separates them: it writes a
non-zero word into a cell, waits, and then clears it. Both forms are pointed at
that one cell, and the two answers must differ in the way the design says.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "probes"))
from asm import Asm, SCRATCH_START  # noqa: E402

WATCHED = SCRATCH_START + 0o20   # 0120: written, then cleared
UNTOUCHED = SCRATCH_START + 0o21  # 0121: never written at all
MARKER = 0o7777
SPACER_MCTS = 8                   # NOOPs between the write and the clear


def build_rope(path: Path) -> None:
    a = Asm()
    marker = a.label("marker")
    zero = a.label("zero")

    a.ca(marker)
    a.ts(WATCHED)                 # 0120 becomes non-zero here
    for _ in range(SPACER_MCTS):  # ... and stays that way for a while
        a.noop()
    a.ca(zero)
    a.ts(WATCHED)                 # 0120 becomes +0 here

    park = a.label("park")
    park.addr = a.here()
    a.tcf(park)

    marker.addr = a.here(); a.word(MARKER)
    zero.addr = a.here();   a.word(0)
    a.write_rope(str(path))


def run(exe: str, rope: Path, *sentinels: str) -> dict[str, str]:
    """Run the frontend and return {argument: 'never' or MCT count}."""
    cmd = [exe, "--rope", str(rope), "--mct", "400"]
    for s in sentinels:
        cmd += ["--sentinel", s]
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    fired = {}
    for line in out.splitlines():
        m = re.match(r"^SENT (\d+) (never|\d+)", line)
        if m:
            fired.setdefault(m.group(1), m.group(2))
    return fired


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    args = ap.parse_args()

    failures = []

    def check(what: str, cond: bool, detail: str = "") -> None:
        if not cond:
            failures.append(f"{what}{': ' + detail if detail else ''}")

    with tempfile.TemporaryDirectory() as tmp:
        rope = Path(tmp) / "sentinel.bin"
        build_rope(rope)

        bare = run(args.exe, rope, f"{WATCHED:04o}")
        at_zero = run(args.exe, rope, f"{WATCHED:04o}=0")
        at_marker = run(args.exe, rope, f"{WATCHED:04o}={MARKER:o}")
        never = run(args.exe, rope, f"{UNTOUCHED:04o}=0")

        key = f"{WATCHED:04o}"
        check("the bare form fires", bare.get(key, "never") != "never")
        check("=V fires on the value it names",
              at_marker.get(key, "never") != "never")
        check("=0 fires on a cell being cleared",
              at_zero.get(key, "never") != "never",
              "this is the moment the bare form cannot see")

        if all(v.get(key, "never") != "never" for v in (bare, at_zero, at_marker)):
            # The marker write and the bare form are the same event, so they must
            # agree exactly; the clear is later by the spacer.
            check("the bare form and =MARKER name the same MCT",
                  bare[key] == at_marker[key],
                  f"{bare[key]} vs {at_marker[key]}")
            check("=0 lands strictly after the value was written",
                  int(at_zero[key]) > int(at_marker[key]),
                  f"{at_zero[key]} is not after {at_marker[key]}")

        # The point of watching the edge rather than the level: cleared erasable
        # is already zero everywhere, and =0 must not call that an event.
        check("=0 stays silent on a cell nothing ever wrote",
              never.get(f"{UNTOUCHED:04o}") == "never",
              "the edge detector fired on the initial state")

    if failures:
        for f in failures:
            print(f"FAIL {f}")
        return 1
    print(f"sentinel: bare and =V agree on {MARKER:o}, and =0 names the clear "
          f"{int(at_zero[key]) - int(at_marker[key])} MCTs later")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
