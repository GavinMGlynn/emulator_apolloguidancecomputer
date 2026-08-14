#!/usr/bin/env python3
"""Run MIT's instruction validation suite and read its verdict off the panel.

The Validation rope does not write a pass/fail cell. It reports through the
DSKY, the same way it would report to a technician standing in front of one:
it puts a code in the PROG display and a sub-code in NOUN, lights OPR ERR, and
waits for the PRO key. `ERRORDSP` in `ext/virtualagc/Validation/Errordsp.agc`
is the whole protocol.

So "did it pass" is not a question about memory, it is a question about what the
machine *displayed* and in what order:

  * **PROG 00** at the start. `INIT` zeroes ERRNUM and calls ERRORDSP
    unconditionally — a checkpoint, not a failure, and the reason a Validation
    rope left alone appears to be sitting on an error.
  * **PROG nn** for any failing test, where nn is the ERRNUM that test had
    reached. Every check increments it, so the code names the check.
  * **PROG 77** at the end: the suite loads MAXERR (077777, which the two-digit
    display shows as 77) and calls ERRORDSP one last time.

A pass is therefore exactly two stops, 00 then 77, with nothing in between.

Afterwards the suite sits in `DONE TCF DONE`, a pure transfer-of-control loop,
and the machine restarts itself on TC TRAP — which is what that alarm is for,
and means a long run shows the suite running over and over.

    tools/validation.py --exe build/.../agc_headless
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROPE = ROOT / "roms" / "Validation" / "Validation.bin"

#: One pass takes about 3.37 million MCTs — some 39 seconds of emulated time,
#: a few seconds of real time on the release build. Run a little past it so the
#: closing PROG 77 is always included.
PASS_MCTS = 3_500_000

STOP = re.compile(r"^STOP (\d+) PROG (\d+) NOUN (\d+)$")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--mct", type=int, default=PASS_MCTS)
    args = ap.parse_args()

    if not ROPE.exists():
        print(f"{ROPE.relative_to(ROOT)} is not built; skipping. "
              f"Run ./tools/build_ropes.sh to assemble it.")
        return 0

    out = subprocess.run(
        [args.exe, "--rope", str(ROPE), "--mct", str(args.mct), "--auto-proceed"],
        capture_output=True, text=True)
    if out.returncode != 0:
        print(f"emulator exited {out.returncode}\n{out.stderr}", file=sys.stderr)
        return 1

    stops = [(int(m.group(1)), m.group(2), m.group(3))
             for line in out.stdout.splitlines()
             if (m := STOP.match(line))]

    # One pass only. After PROG 77 the suite sits in its DONE loop, TC TRAP
    # restarts the machine and the whole thing runs again, so anything past the
    # first completion is the *next* pass's opening checkpoint rather than a
    # late failure.
    for i, (_, prog, _) in enumerate(stops):
        if prog == "77":
            stops = stops[: i + 1]
            break

    for mct, prog, noun in stops:
        print(f"  MCT {mct:>9}  PROG {prog} NOUN {noun}")

    if not stops:
        print("the suite displayed nothing at all — it is not running", file=sys.stderr)
        return 1

    codes = [prog for _, prog, _ in stops]
    if codes[0] != "00":
        print(f"expected the suite's opening checkpoint (PROG 00), got {codes[0]}",
              file=sys.stderr)
        return 1

    failures = [(mct, prog, noun) for mct, prog, noun in stops[1:] if prog != "77"]
    if failures:
        for mct, prog, noun in failures:
            print(f"FAILED: the suite stopped at MCT {mct} on check {prog} "
                  f"(sub-code {noun})", file=sys.stderr)
        return 1

    if "77" not in codes:
        print(f"the suite did not reach its closing PROG 77 within {args.mct} MCT — "
              f"it is still running, or it hung", file=sys.stderr)
        return 1

    print(f"MIT's instruction validation suite passes: "
          f"{len(stops)} stop(s), opening checkpoint then PROG 77, no check failed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
