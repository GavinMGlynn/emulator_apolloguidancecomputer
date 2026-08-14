#!/usr/bin/env python3
"""Drive a flight rope's DSKY the way an astronaut would, and check it answers.

Every other test in this project checks a part. This one checks that the parts
add up: a real Apollo rope is loaded, the flight software boots, a human keys at
it, and the answer comes back out through the relay words onto the panel. If any
link in that chain is wrong — the rope image, the instruction set, the timing,
priority control, the keyboard interrupt, PINBALL, channel 10, the relay decode
— this stops showing 8s.

The sequence is the documented one. Virtual AGC's own LM tutorial: *"After a
fresh AGC start there is a need to do a reset by typing V36E."* Then **V35E**,
which is the DSKY's own lamp test: the flight software fills every digit of
every field with 8, sets all three signs, and flashes. It is the single best
end-to-end assertion available, because the rope has to drive all seven display
fields to pass it and nothing partial looks like success.

This is also the answer to a question that was open for a long time (FINDINGS
#59, #81): these ropes reach no display from a cold start, and that is the rope
being correct rather than the emulator being broken. They are waiting to be
talked to. Talked to, they answer.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

#: Rope -> the keys to send. Both are flown ropes: the Apollo 11 LM and CM.
ROPES = {
    "Luminary099": "roms/Luminary099/Luminary099.bin",
    "Comanche055": "roms/Comanche055/Comanche055.bin",
}

#: V36 ENTER (fresh start), then V35 ENTER (lamp test). Spaced by 40 000 MCTs —
#: about half a second of AGC time. The flight software's keyboard scan runs at
#: 200 Hz and each press is held for a tenth of a second, so this is a slow,
#: unambiguous human rather than a machine racing the software.
KEYS = [("V", 200_000), ("3", 240_000), ("6", 280_000), ("E", 320_000),
        ("V", 700_000), ("3", 740_000), ("5", 780_000), ("E", 820_000)]

MCTS = 1_000_000  # ~11.7 s of AGC time; the lamp test is fully lit by ~865 000

#: What a lamp test looks like. Every field at its maximum, both signs positive.
LAMP_TEST = ("PROG 88 VERB 88 NOUN 88 "
             "R1 +88888 R2 +88888 R3 +88888")


def run(exe: str, rope: Path) -> list[str]:
    cmd = [exe, "--rope", str(rope), "--mct", str(MCTS), "--trace-dsky"]
    for key, mct in KEYS:
        cmd += ["--press", f"{key}:{mct}"]
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    return [ln for ln in out.splitlines() if ln.startswith("DSKY ")]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", required=True)
    args = ap.parse_args()

    failures = []
    for name, relative in ROPES.items():
        rope = ROOT / relative
        if not rope.exists():
            print(f"SKIP {name}: {relative} is not built")
            continue

        lines = run(args.exe, rope)
        if not lines:
            failures.append(f"{name}: the panel never changed at all")
            continue

        # Cold start: blank, and staying blank until spoken to is the point.
        first = lines[0]
        if "88" in first:
            failures.append(f"{name}: the panel was not blank at MCT 0 — {first}")

        lit = [ln for ln in lines if LAMP_TEST in ln]
        if not lit:
            failures.append(
                f"{name}: the lamp test never lit every field. Last panel was "
                f"{lines[-1].split(maxsplit=2)[-1]!r}"
            )
            continue

        # The AGC flashes the lamp test; a lamp test that never flashed would
        # mean the flash bit is not reaching the panel.
        if not any("FLASH" in ln for ln in lit):
            failures.append(f"{name}: the lamp test lit but never flashed")

        mct = lit[0].split()[1]
        print(f"{name}: lamp test fully lit at MCT {mct}, flashing")

    if failures:
        for f in failures:
            print(f"FAIL {f}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
