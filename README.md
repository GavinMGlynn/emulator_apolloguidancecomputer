# agc — a cycle-correct Apollo Guidance Computer

A Block II Apollo Guidance Computer emulated at the **control-pulse level**, in
C23. The machine cycle here is the AGC's own: one `agc_tick()` is one timing
pulse of the 1.024 MHz clock, twelve of them make the 11.71875 µs Memory Cycle
Time, and at each one the sequence generator asserts the control pulses that
AGC4 Memo #9 says it should. There is no instruction-level shortcut anywhere in
the core — that is the entire point.

It runs the flight software. Apollo 11's `Luminary099` and `Comanche055`, Apollo
13/14's `Luminary131`, Apollo 15–17's `Artemis072`, and MIT's own instruction
validation rope all boot and execute.

```
$ ./tools/build_ropes.sh
$ cmake --preset linux-release && cmake --build --preset linux-release
$ ./build/linux-release/src/frontend/headless/agc_headless \
      --rope roms/Luminary099/Luminary099.bin --mct 12 --trace-mct
TC0    T1  A=000000 L=000000 Q=000000 Z=000000 G=000000 B=004000 S=4000 SQ=00 ...
STD2   T1  A=000000 L=000000 Q=000000 Z=004001 G=000004 B=004001 S=4001 SQ=04 ... INH=1
CA0    T1  A=000000 L=000000 Q=000000 Z=004002 G=034054 B=034054 S=4054 SQ=34 ...
XCH0   T1  A=012103 L=000000 Q=000000 Z=004003 G=156006 B=156006 S=6006 SQ=56 ...
```

That third line is the AGC executing `INHINT` — the pseudo-code that Luminary's
`GOPROG` starts with — and the interrupt-inhibit flip-flop coming up set,
because the hardware recognises a `TC 4` by its *address* during the fetch.

## Why control-pulse level

An AGC instruction is not a unit of work. It is a sequence of **subinstructions**
(`TC0`, `CCS0`, `MP1`, `DV3`…), each occupying exactly one Memory Cycle Time, and
each MCT is twelve timing pulses T1–T12 during which the cross-point generator
asserts zero to five control pulses — `RA`, `WY`, `CI`, `TSGN`, `ZIP` — onto a
wired-OR write bus. Model that faithfully and a great deal stops needing to be
modelled at all:

- **Counter interleaving is emergent.** Peripherals do not DMA; they raise a
  request on one of 29 counter cells, and the sequence generator steals whole
  MCTs from the program to service them. Under enough counter traffic the
  program stops making progress. That is the Apollo 11 1201/1202 mechanism, and
  here it falls out of priority control rather than being simulated.
- **The destructive core-memory cycle is real.** Erasable reads happen before
  T5, destroy the word, and are rewritten before T10 — which is why the editing
  registers at 0020–0023 shift on write, and why `WG` has to remember the
  address the cycle started with.
- **Ones' complement behaves.** Two zeroes, an end-around carry, and a `TMZ`
  pulse that exists solely because `+0` and `-0` are numerically equal but not
  bit-equal.
- **The alarms are just edge detectors on the scaler.** TC TRAP, RUPT LOCK and
  NIGHT WATCHMAN each ask "did the expected thing happen before this clock
  edge?", raise a bit in channel 77, and force a GOJAM.

## Status

| Subsystem | State |
|---|---|
| Sequence generator, all 57 subinstructions | working, tables generated from the memo |
| Central registers, adder, banking | working |
| Erasable + fixed memory, parity, superbanks | working |
| Scaler, timers, the three alarms | working |
| Priority control: counters, interrupts | working (PINC/MINC/PCDU/MCDU/DINC) |
| Probe suite + golden regression | working — 26 instruction timings asserted against the memo |
| Serial counters (SHINC/SHANC), CDU, IMU, DSKY | **not yet** |

Instruction timing is not just frozen in a golden, it is *checked against the
memo*. All 26 instructions the timing probe measures come out at exactly the MCT
counts AGC4 Memo #9's sequence tables predict — including divide, where DVST
lets a sub-sequence end at T3 instead of T12, so the fact that a divide totals
exactly 72 timing pulses was a result rather than an assumption. A separate
probe measures a peripheral stealing 31 whole MCTs from a program that never
interacted with it: the Apollo 11 1201/1202 mechanism, emergent.

**To build it, load a rope and drive the DSKY, see `docs/USING_THE_AGC.md`** —
including the cold-start sequence, because a flight rope shows you nothing until
you key V36E at it, and that is the rope being correct rather than the emulator
being broken.

See `docs/PROJECT_STATUS.md` for what is verified and how, and
`docs/COMPLETION_PLAN.md` for the road to done. Deliberate approximations are
listed with their reason and cost to close; nothing is left out quietly.

## Layout

```
src/core/        the emulator: a static library with zero frontend dependencies
  cpu/           registers, sequence generator, control pulses, pulse tables
  memory/        erasable and fixed memory, banking, rope parity
  timing/        the 17-stage scaler and the hardware alarms
  io/            channels, counter cells, interrupts
src/frontend/
  headless/      deterministic, no wall clock, no host input — the probe engine
tests/           one Unity suite per subsystem, one CTest entry each
tools/           build_ropes.sh, gen_subinst_tables.py, oracle findings
docs/references/ the hardware documents every number in the core cites
ext/             pinned submodules: Unity, and the three AGC references
roms/  bios/     gitignored media, rebuilt by tools/build_ropes.sh
```

## Building

Needs CMake ≥ 3.21, Ninja, and Clang.

```
git submodule update --init ext/unity      # all CI needs
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --preset linux-debug                 # must be green
ctest --preset linux-release               # same results, bit for bit
```

Emulated results are AGC timing-pulse counts, not wall-clock measurements, so
they are identical on every platform and build type. CI asserts that across
Linux, RHEL, macOS and Windows; a host where a golden differs has a real bug.

To get the flight software:

```
git submodule update --init ext/virtualagc  # large
./tools/build_ropes.sh                      # assembles roms/, stages bios/
```

`build_ropes.sh` compiles yaYUL from the Virtual AGC submodule and assembles
each rope from the original MIT/Draper listings with that rope's own dialect
flags, in the physical `--hardware` bit layout (data bits 1–14, parity in
position 15, data bit 15 in position 16). It also stages the Block II
**rope-module dumps** — words read out of real core-rope hardware, several of
them carrying genuine ageing defects — into `bios/`, because on this machine the
rope *is* the firmware.

## References and oracles

The core cites its sources. Three of them matter most:

- **AGC4 Memo #9**, Hugh Blair-Smith, 1966 — the control-pulse definitions and
  the T1–T12 sequence tables. `docs/references/AgcPulsesAndSequences.txt` is
  Blair-Smith's own transcription and is the primary authority here.
- **[AGCPlusPlus](https://github.com/CaptainSwag101/AGCPlusPlus)** (MIT) — a
  control-pulse-level model at the same abstraction as this one, developed with
  Mike Stewart. The memo has typos and omits pulses the hardware asserts
  implicitly; this is where those are resolved.
  `tools/gen_subinst_tables.py` transcribes the corrected tables mechanically
  rather than by hand, and reports every divergence from the memo it finds.
- **[agc_simulation](https://github.com/virtualagc/agc_simulation)** and the
  original NOR-gate schematics in `docs/references/block2-schematics/` — gate
  truth, for when the memo and the model disagree.

`docs/references/` is committed except for one 94 MB PDF; `./tools/fetch_docs.sh`
re-downloads anything missing.

**[Virtual AGC](https://github.com/virtualagc/virtualagc)** (GPL) supplies yaYUL
and the rope listings. Its yaAGC is instruction-level: useful for architectural
state, useless for timing, and never a source of a cycle count here. It is
vendored as a submodule and read; none of it is compiled or linked into this
emulator.

`docs/references/README.md` indexes the rest.

## Licence

MIT — use it for anything, no warranty. See `LICENSE`, which also records the
terms of the vendored components and the provenance of the historical documents.
