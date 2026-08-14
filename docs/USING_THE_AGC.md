# Starting and using the AGC

How to build this emulator, put a rope in it, and drive the machine from the
DSKY. Everything below is a command you can paste; the DSKY sequences were run
against the ropes in `roms/` and the traces are the real output.

If you want to know *why* the emulator is built the way it is, read `CLAUDE.md`
and `docs/PROJECT_STATUS.md` instead. This document is the operating manual.

---

## 1. Build

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --preset linux-debug            # 20 suites; must be green
```

There is a release preset too, and it is the one to use for anything that runs
a rope for more than a second or two — the debug build is roughly an order of
magnitude slower, and every number this emulator produces is identical between
them by design.

```sh
cmake --preset linux-release && cmake --build --preset linux-release
```

Other presets: `linux-gcc` (a second compiler, because gcc-only warnings are
real), `macos-*`, `windows-*`. The `*-ci` presets are what the pipeline runs.

## 2. Get the ropes

`roms/` and `bios/` are gitignored: they are rebuilt from the Virtual AGC
submodule rather than committed.

```sh
git submodule update --init ext/virtualagc
./tools/build_ropes.sh
```

That assembles the flight and test ropes with yaYUL and stages the physical
rope-module dumps. Afterwards `roms/` holds:

| Rope | What it is |
| ---- | ---------- |
| `Luminary099` | Apollo 11 LM — the Eagle rope |
| `Comanche055` | Apollo 11 CM |
| `Artemis072`, `Zerlina56`, `Luminary131` | later flight and development ropes |
| `Validation` | Ed Smally's instruction validation suite |
| `Aurora12` | Block II development rope with a self-check |
| `SundialE` | erasable/fixed exerciser |
| `Retread50` | small early rope, boots fast |
| `LM131R1` | a physical rope article, read out of real modules |

## 3. Run it — headless

```sh
build/linux-release/src/frontend/headless/agc_headless \
    --rope roms/Luminary099/Luminary099.bin --mct 200000 --dump-dsky
```

`--mct N` is the run length in Memory Cycle Times. One MCT is 11.71875 µs, so
**85,333 MCTs is one second of AGC time**. A few useful conversions:

| Wall time | MCTs |
| --------- | ---- |
| 100 ms | 8,533 |
| 1 s | 85,333 |
| 10 s | 853,333 |
| 1 min | 5,120,000 |

`--timepulses N` is the same thing at twelve times the resolution, for when you
care about individual control pulses.

### Seeing what it is doing

| Flag | Use it for |
| ---- | ---------- |
| `--trace-dsky` | print the panel every time it changes — the single most useful flag |
| `--dump-dsky` | print the panel once, at the end |
| `--trace-mct` | machine state at the end of every MCT |
| `--trace` | machine state at every timing pulse (very large output) |
| `--dump-state` | registers and the MCT count when the run ends |
| `--dump-mem A[:LEN]` | dump erasable words from octal address A |
| `--dump-channels` | every I/O channel |
| `--dump-counters` | the counter cells and pending interrupts |

## 4. Run it — the SDL DSKY

```sh
build/linux-release/src/frontend/sdl/agc_dsky --rope roms/Luminary099/Luminary099.bin
```

Keys: `0`-`9`, `V` verb, `N` noun, `Return` enter, `R` reset, `C` clear,
`K` key release, `=` plus, `-` minus, `P` proceed, `Esc` quit.

For a screenshot without a display attached:

```sh
build/linux-release/src/frontend/sdl/agc_dsky \
    --rope roms/Luminary099/Luminary099.bin --frames 600 --screenshot /tmp/dsky.png
```

## 5. Starting a flight rope from cold

**A flight rope shows you nothing until you talk to it, and that is correct.**
Luminary, Comanche, Artemis and Zerlina all come up with the panel blank. They
are not hung — they are waiting. Virtual AGC's own LM tutorial puts it plainly:
*"After a fresh AGC start there is a need to do a reset by typing V36E."*

So the cold-start sequence is **V36E**, then **V37E 00E** to select the idle
program.

Headless, `--press KEY:MCT` presses a key at a given MCT. Space them by a few
tens of thousands of MCTs — the flight software's keyboard scan runs at 200 Hz
and each keypress is held for a tenth of a second:

```sh
build/linux-release/src/frontend/headless/agc_headless \
    --rope roms/Luminary099/Luminary099.bin --mct 4000000 --trace-dsky \
    --press V:200000 --press 3:240000 --press 6:280000 --press E:320000 \
    --press V:600000 --press 3:640000 --press 7:680000 --press E:720000 \
    --press 0:900000 --press 0:940000 --press E:980000
```

which produces:

```
DSKY 0       PROG    VERB    NOUN    R1        R2        R3
DSKY 247754  PROG    VERB 3  NOUN    R1        R2        R3
DSKY 288711  PROG    VERB 36 NOUN    R1        R2        R3
DSKY 329680  PROG    VERB    NOUN    R1        R2        R3
DSKY 643697  PROG    VERB 3  NOUN    R1        R2        R3
DSKY 684658  PROG    VERB 37 NOUN    R1        R2        R3
DSKY 720322  PROG    VERB 37 NOUN    R1        R2        R3        FLASH
DSKY 909937  PROG    VERB 37 NOUN 0  R1        R2        R3        FLASH
DSKY 940658  PROG    VERB 37 NOUN 00 R1        R2        R3        FLASH
DSKY 980255  PROG    VERB 37 NOUN 00 R1        R2        R3
DSKY 1012363 PROG    VERB    NOUN    R1        R2        R3
```

Read that as the machine working: each digit appears as it is keyed, V36 clears
the display on ENTER, and **V37 flashes** — which is the AGC asking for the
two-digit program number — and stops flashing when it gets one.

The `--press` keys are `0`-`9`, `V`, `N`, `E` (enter), `R` (reset), `C` (clear),
`K` (key release), `+`, `-`. `--uplink KEY:MCT` sends the same keystroke from
the ground instead, as the triple-redundant uplink word.

### Ropes that do display on their own

`Validation` and `SundialE` drive the panel without being asked, because they
are test programs rather than flight software. If you want to see a display
immediately and check the emulator end to end, run one of those.

## 6. Running the MIT validation suite

The Validation rope stops at every checkpoint and every failure and waits for
PROCEED. `--auto-proceed` presses it and reports where it stopped:

```sh
build/linux-release/src/frontend/headless/agc_headless \
    --rope roms/Validation/Validation.bin --mct 3000000 --auto-proceed
```

```
STOP 37526 PROG 00 NOUN 00
STOPS 1
```

**PROG 77 means the suite finished.** Any other PROG/NOUN pair at a stop is a
failure cell, and `docs/references/TEST_SOFTWARE.md` says how to read it. The
whole suite runs as the `mit_validation_suite` test.

## 7. Timing a piece of software

`--sentinel` is how a probe marks a moment. The harness measures the time, not
the program, because the AGC's finest software-readable clock is TIME6 at
1.6 kHz — far too coarse to time an instruction.

```sh
--sentinel 0120        # report the MCT at which cell 0120 first becomes non-zero
--sentinel 0120=0      # report the MCT at which it *changes to* +0
```

The `=V` form is the only one that can mark a moment whose value is zero — a
counter arriving, a flag coming down. The run stops once every sentinel has
fired, so `--mct` is an upper bound rather than the intent.

`tools/probes/README.md` explains how to write a probe; the committed ones are
regenerated by `tools/probes/gen_*.py` and checked by `tools/regress.py`.

## 8. When something looks wrong

| Symptom | First thing to check |
| ------- | -------------------- |
| Panel blank on a flight rope | Expected. Key V36E, then V37E 00E — see §5. |
| `UNDECODED n` printed | A defect in our subinstruction tables: the decoder found no sequence for an (SQ, ST, EXTEND) triple. It should never appear. |
| `alarm=1` in `--dump-state` | A hardware alarm latched. `--dump-channels` shows channel 77's bits; `--inhibit-alarm N` suppresses one of them for a diagnostic run. |
| Rope won't load | `roms/` is gitignored — run `./tools/build_ropes.sh`. |
| Nothing happens at all | Check the rope actually loaded: the frontend prints `loaded N words`. |

Three flags exist purely to bisect a problem by taking the machine apart, and
each one makes it less of an AGC — use them to answer a question, never to make
something pass: `--ignore-alarms`, `--ignore-counters`, `--ignore-interrupts`.

## 9. Checking the emulator against the hardware

These are the oracles, in the order `CLAUDE.md` says to trust them.

```sh
# Every subinstruction, every branch condition, all twelve timing pulses,
# read out of the gate netlists and diffed against our tables.
python3 tools/oracle/gate_diff.py
#   1672 rows checked across 36 subinstructions: 0 disagree

# One subinstruction's timeline, straight from the gates.
python3 tools/oracle/gate_crosspoint.py MP3 --branch 1x
python3 tools/oracle/gate_crosspoint.py --list

# Differential test against the runnable reference model.
ctest --preset linux-debug -R oracle_differential
```

The gate-level tools need `ext/agc_simulation`; the differential test needs
`ext/agcplusplus`. Both are submodules, and every test that needs one skips
cleanly when it is absent, so CI stays green with only `ext/unity`.

`tools/oracle/FINDINGS.md` records every place the three sources disagreed and
which one won. It is worth reading before changing anything in `src/core/cpu`.
