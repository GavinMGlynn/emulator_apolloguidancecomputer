# CLAUDE.md — working conventions

Cycle-correct Apollo Guidance Computer emulator. **Block II** (the flown
machine; Block I is out of scope until Block II is done). Principles from
`../emulator-setup-guide.md`; this file is the short operational version.

## What "cycle-correct" means here
The AGC's machine cycle is the **timing pulse**, not the instruction. The
2.048 MHz master oscillator is divided to a 1.024 MHz timing-pulse clock;
**twelve timing pulses T1–T12 make one Memory Cycle Time (MCT) of 11.71875 µs**,
and every subinstruction occupies exactly one MCT. At each timing pulse the
sequence generator asserts zero to five **control pulses** (RA, WY, CI, TSGN …)
onto the write lines.

Our reference core therefore ticks **once per timing pulse** and executes the
control pulses for that pulse, in canonical order. There is no instruction-level
shortcut anywhere in `src/core` — that is the whole point, and it is what makes
counter interleaving, DV staging, and the T4/T10 memory windows emergent rather
than special-cased.

## The three oracles (internalise this)
- **Control-pulse truth → `docs/references/AgcPulsesAndSequences.txt`**, Hugh
  Blair-Smith's transcription of pages 30–50 of **AGC4 Memo #9**. This is the
  primary source: it lists, for every subinstruction, which control pulses fire
  at which timing pulse under which branch-register condition. Our
  `src/core/cpu/subinst.c` tables are a direct transcription of it and each
  entry must stay diffable against the memo.
- **Corrected/runnable model → `ext/agcplusplus`** (MIT). A control-pulse-level
  emulator at the same abstraction as ours, built with Mike Stewart's help. The
  memo has known typos and omissions (e.g. the `WG` in WRITE0 T2, the implicit
  `1xP10`/`8xP5` pulses in divide); AGCPlusPlus is where those are resolved.
  Read it — do not link it.
- **Gate truth → `ext/agc_simulation`** (Mike Stewart's gate-level Block II
  model) and `docs/references/block2-schematics/` (the original module-level
  NOR-gate schematics: SCALER, TIMER, CROSS-POINT GENERATOR, COUNTER CELL,
  ALARMS …). When the memo and AGCPlusPlus disagree, the gates decide.

`ext/virtualagc` (GPL) is the fourth reference: **yaAGC** is an
instruction-level emulator — useful for architectural state and for assembling
ropes with **yaYUL**, useless for timing. Never take a cycle count from it.

## Discipline
- **Reference-first.** Resolve doubt from the memo, AGCPlusPlus, or the
  schematics — never trial-and-error on our own parameters. Characterise a
  discrepancy (which timing pulse? which branch condition?) before fixing.
- **Complete modules, don't chase the PC.** Finish one subsystem with tests
  before moving on. Booting a rope to a DSKY display is a thermometer, never a
  milestone.
- **One item at a time, landing with its test.** Keep `ctest` green — a red tree
  is the stop-everything condition.
- **Test behaviour as hardware facts**, named as sentences
  (`test_ccs_leaves_the_diminished_absolute_value_in_a`).
- **Measure, don't guess** — timing from the pulse tables, on the release build
  only. Placeholder numbers are marked PROVISIONAL in code *and* status doc.
- **Verify on real output** — a booted rope's DSKY channel traffic, or the
  Validation suite's own pass/fail cells, not a proxy.
- **Temporary instrumentation is always reverted** before commit (ours *and* any
  `ext/` checkout) — edit-revert-restore, never `git checkout` over live work.
- **Optimization only under an identity harness** — probe goldens + a long-run
  state hash, byte-identical or it doesn't ship.

## Every commit that lands an item updates both living docs in the same commit
`docs/PROJECT_STATUS.md` (what now works + its verification) and
`docs/COMPLETION_PLAN.md` (tick the item; add tails discovered while
implementing). Co-author trailer on commits; PRs via the platform CLI.

## Build & test
```
cmake --preset linux-debug   && cmake --build --preset linux-debug
ctest --preset linux-debug                 # must be green
ctest --preset linux-release               # goldens must be bit-identical
```
Repopulate the (gitignored) media:
```
./tools/build_ropes.sh      # assembles roms/*, stages bios/rope-modules/*
```

## Layout
`src/core` is a static lib with **zero** frontend deps, one directory per
subsystem: `cpu` (registers, sequence generator, control pulses,
subinstruction tables), `memory` (erasable/fixed, banking, parity),
`timing` (17-stage scaler, alarms), `io` (channels, counter cells, interrupts),
`dsky`. Frontends (`headless`, later `sdl`) depend on the core, never the
reverse. `ext/` never gets our warning set. `roms/` and `bios/` are gitignored
media rebuilt by `tools/build_ropes.sh`.
