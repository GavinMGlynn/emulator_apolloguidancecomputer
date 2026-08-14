#!/usr/bin/env python3
"""Check the emulator against physical rope-memory modules.

`bios/rope-modules/` holds dumps read back from real Apollo-era rope modules,
including one that was dumped while defective and the same module after repair.
That makes three things checkable that nothing assembled from source can check:

1. **The rope image layout, end to end.** LM131R1 assembled from its own source
   must equal the dump taken off the physical article, word for word. That
   exercises the assembler invocation, the `--hardware` bit layout, the odd
   parity rule and the bank ordering all at once, against hardware.
2. **Odd parity, on real rope.** Every word of every clean module must carry it.
3. **The PARITY FAIL alarm, on real damage.** Booting the defective module has
   to raise it, and booting the repaired one must not — the alarm proving
   itself against a fault nobody simulated.

Two things about the dumps have to be undone before any of that works, and both
are the kind of quiet mismatch that produces a machine that almost runs:

  * they are in yaAGC's `--parity` word layout, not the `--hardware` layout our
    fixed memory stores;
  * the first four banks of a rope image are stored 02, 03, 00, 01.

`agc_memory_load_module` does both.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODULES = ROOT / "bios" / "rope-modules"

RETREAD_B1_BAD = "2003053-031-BLK2-Retread50-B1-BadBits.bin"
RETREAD_B1_OK = "2003053-031-BLK2-Retread50-B1-Repaired.bin"
RETREAD_B2 = "2003053-041-BLK2-Retread50-B2.bin"
LM131R1_DUMP = "Luminary131PlusLM131R1ModuleDump.bin"

ALARM_PARITY_FAIL = 0o1

#: Slot -> bank for a rope image; only the first four are out of order.
IMAGE_ORDER = [2, 3, 0, 1] + list(range(4, 36))


def words(path: Path) -> list[int]:
    data = path.read_bytes()
    return [(data[i] << 8) | data[i + 1] for i in range(0, len(data), 2)]


def odd_parity(word: int) -> bool:
    bits = 0
    while word:
        bits ^= word & 1
        word >>= 1
    return bits == 1


def hardware_to_parity_layout(word: int) -> int:
    """Our stored layout -> the layout the dumps are in."""
    data = (word & 0o37777) | (((word >> 15) & 1) << 14)
    return (data << 1) | ((word >> 14) & 1)


def check_layout(failures: list[str]) -> None:
    ours = ROOT / "roms" / "LM131R1" / "LM131R1.bin"
    dump = MODULES / LM131R1_DUMP
    if not ours.exists():
        print(f"  skip: {ours.relative_to(ROOT)} is not assembled")
        return

    a, b = words(ours), words(dump)
    differ = sum(
        1
        for slot, bank in enumerate(IMAGE_ORDER)
        for i in range(1024)
        if hardware_to_parity_layout(a[bank * 1024 + i]) != b[slot * 1024 + i]
    )
    if differ:
        failures.append(f"LM131R1 differs from its module dump in {differ} words")
    else:
        print(f"  LM131R1 assembled from source == the physical module dump, "
              f"all {len(a)} words")


def check_parity(failures: list[str]) -> None:
    for path in sorted(MODULES.glob("*BLK2*.bin")):
        bad = [w for w in words(path) if w and not odd_parity(w)]
        defective = "BadBits" in path.name
        if defective and not bad:
            failures.append(f"{path.name} is a known-defective dump but every word "
                            f"passes parity")
        elif not defective and bad:
            failures.append(f"{path.name} has {len(bad)} words failing parity")
        else:
            note = f"{len(bad)} parity failures, as dumped" if bad else "clean"
            print(f"  {path.name:52s} {note}")


def alarms(exe: str, b1: str, mct: int) -> int:
    out = subprocess.run(
        [exe, "--module", f"1:{MODULES / b1}", "--module", f"2:{MODULES / RETREAD_B2}",
         "--mct", str(mct), "--dump-channels"],
        capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit(f"emulator exited {out.returncode}\n{out.stderr}")
    for line in out.stdout.splitlines():
        if line.startswith("CH 77 "):
            return int(line.split()[2], 8)
    return 0


def check_alarm(exe: str, mct: int, failures: list[str]) -> None:
    bad = alarms(exe, RETREAD_B1_BAD, mct)
    ok = alarms(exe, RETREAD_B1_OK, mct)
    if not bad & ALARM_PARITY_FAIL:
        failures.append(f"the defective module did not raise PARITY FAIL "
                        f"(channel 77 = {bad:06o})")
    else:
        print(f"  the defective module raises PARITY FAIL (channel 77 = {bad:06o})")
    if ok & ALARM_PARITY_FAIL:
        failures.append(f"the repaired module raised PARITY FAIL "
                        f"(channel 77 = {ok:06o})")
    else:
        print(f"  the repaired module does not (channel 77 = {ok:06o})")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--mct", type=int, default=50_000)
    args = ap.parse_args()

    if not (MODULES / RETREAD_B1_BAD).exists():
        print("bios/rope-modules/ is empty; skipping. "
              "Run ./tools/build_ropes.sh to stage the dumps.")
        return 0

    failures: list[str] = []
    print("rope image layout, against a physical module:")
    check_layout(failures)
    print("odd parity across every dumped module:")
    check_parity(failures)
    print("the PARITY FAIL alarm, against real damage:")
    check_alarm(args.exe, args.mct, failures)

    for f in failures:
        print(f"FAILED: {f}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
