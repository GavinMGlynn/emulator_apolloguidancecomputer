#!/usr/bin/env python3
"""Smoke-test the interactive DSKY without a screen.

Runs the SDL frontend under SDL's dummy video driver for a bounded number of
frames and checks that the panel shows what the headless frontend reads off the
same rope. That makes the graphical frontend checkable in CI, which is the only
way it stays honest: a display that silently stops updating looks fine.

Sundial E is the rope used because it is the one that puts a real reading up —
VERB 05 NOUN 31 with its alarm code in R2 (see FINDINGS). The flight ropes sit
blank from a cold start and would prove nothing.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROPE = ROOT / "roms" / "SundialE" / "SundialE.bin"
EXPECT = "PROG 00 VERB 05 NOUN 31 R1  00000 R2  01107 R3  00000"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--frames", type=int, default=240)
    ap.add_argument("--screenshot")
    args = ap.parse_args()

    if not Path(args.exe).exists():
        print(f"{args.exe} was not built (no SDL3?); skipping.")
        return 0
    if not ROPE.exists():
        print(f"{ROPE.relative_to(ROOT)} is not assembled; skipping.")
        return 0

    cmd = [args.exe, "--rope", str(ROPE), "--frames", str(args.frames), "--dump-dsky"]
    if args.screenshot:
        cmd += ["--screenshot", args.screenshot]

    env = dict(os.environ, SDL_VIDEODRIVER="dummy")
    out = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if out.returncode != 0:
        print(f"the frontend exited {out.returncode}\n{out.stderr}", file=sys.stderr)
        return 1

    panel = next((l[len("DSKY "):] for l in out.stdout.splitlines()
                  if l.startswith("DSKY ")), None)
    if panel is None:
        print("the frontend printed no panel", file=sys.stderr)
        return 1

    print(f"  {panel}")
    if panel.strip() != EXPECT:
        print(f"FAILED: expected\n  {EXPECT}", file=sys.stderr)
        return 1
    print("the interactive DSKY draws Sundial E's display, headless.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
