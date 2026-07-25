# Completion plan

Phased road to done. **Every item names its verification** — an item without one
cannot be ticked. New tails go in the moment they are discovered, not when
someone remembers.

Legend: `[x]` done · `[ ]` open · `[~]` partially done, with the remainder
spelled out.

---

## Phase 0 — Foundations ✅

- [x] Tree, CMake/Ninja/Clang C23, presets, strict warnings + `-Werror`.
      *Verified:* debug and release both build clean.
- [x] Four-platform CI (Linux, Rocky, macOS, Windows) running both build types.
      *Verified:* workflow committed before the first subsystem, per §3 of the
      guide. **Needs a first green run once the repo has a remote.**
- [x] Unity vendored; one CTest entry per suite.
      *Verified:* `ctest` green.
- [x] References mirrored into `docs/references/`; oracles vendored in `ext/`.
      *Verified:* `docs/references/README.md` indexes them.
- [x] `tools/build_ropes.sh` reproduces `roms/` and `bios/` from the Virtual AGC
      submodule.
      *Verified:* nine ropes assemble; parity checked over the whole image.

## Phase 1 — The reference core ✅

- [x] Timing-pulse-stepped machine: `agc_tick()` = one timing pulse, three
      phases (before / pulses / after).
      *Verified:* `timing_suite` pins the 12-pulse MCT and the scaler divisor.
- [x] All 57 subinstructions as data tables, generated from the memo as
      corrected by AGCPlusPlus.
      *Verified:* `subinst_tables_are_current` in CTest; divergences catalogued
      in `tools/oracle/FINDINGS.md`.
- [x] All control pulses implemented, including the three the memo omits.
      *Verified:* `cpu_suite`; the generator now hard-errors rather than
      silently dropping an unmapped signal (FINDINGS #16).
- [x] Erasable and fixed memory, banking, superbanks, rope parity.
      *Verified:* `memory_suite`.
- [x] Scaler, timers and the four hardware alarms.
      *Verified:* `timing_suite`, one test per alarm.
- [x] Priority control: counter cells, involuntary sequences, interrupts.
      *Verified:* `timing_suite`.
- [x] Deterministic headless frontend.
      *Verified:* nine ropes boot; debug and release dumps byte-identical.

## Phase 2 — Verification (the crown jewel; next)

This is what turns "cycle-correct by construction" into a checkable claim.

- [ ] **Bare-metal probe framework.** A Python encoder in `tools/probes/`
      emitting rope images directly — no toolchain — so a probe is a few dozen
      hand-assembled words that make the machine measure itself: read TIME1/TIME2,
      do the thing, store the delta at a fixed erasable address, write a
      sentinel.
      *Verification:* a probe measuring a known-length instruction returns the
      MCT count the memo predicts.
- [ ] **One probe per instruction class**, measuring subinstruction counts:
      single-MCT (CA, AD, TS…), two-MCT (DAS, DXCH, DCA…), MP (3), DV (6),
      the branch instructions' taken/not-taken asymmetry.
      *Verification:* each measured count matches the memo's sequence tables.
- [ ] **Probes for the emergent behaviour** unit tests cannot reach: a counter
      request landing mid-instruction, a counter storm starving the program,
      an interrupt refused because A holds overflow, RESUME restoring
      Z/BRUPT exactly.
      *Verification:* measured against the pulse tables and the gate-level
      reference where the tables are silent.
- [ ] **Golden regression.** `tools/regress.py` wired into CTest, running every
      probe and diffing its result block against a checked-in golden.
      *Verification:* goldens bit-identical on all four CI platforms and both
      build types — they are timing-pulse counts, so anything else is a real bug.
- [ ] **Long-run state hashes** for each rope at fixed MCT counts, as the
      identity harness for any future optimization.
      *Verification:* the hash is stable across platforms and build types.
- [ ] **Instrument the gate-level oracle** (`ext/agc_simulation`) to settle the
      four open rows in `tools/oracle/FINDINGS.md` (#9 DAS1 T10/T11, #10 DV
      DVST placement, #11 MP3 T6/T12, #13 DV4 T3).
      *Verification:* a captured pulse timeline per question, recorded in
      FINDINGS with the instrumentation reverted afterwards.

## Phase 3 — Peripherals

- [ ] **DSKY.** Channel 10 display decode (the digit-group encoding), channel 11
      lamps, channels 15/16 keyboard input, KEYRUPT, the verb/noun flash driven
      from scaler stages 16–17.
      *Verification:* a rope's startup display read out of the channel traffic
      and compared against the listing's expected V/N; scripted key presses at
      given MCTs in the headless frontend.
- [ ] **Serial counters SHINC/SHANC**, closing the dropped-request approximation.
      *Verification:* a probe shifting a known word into INLINK and reading it
      back; the counter-request path no longer silently discards.
- [ ] **CDU** — the five angle counters, the error counters, coarse align, CDU
      ZERO. Closes the POUT/MOUT approximation.
      *Verification:* probes on PCDU/MCDU pulse trains against the counter
      sequences; channel 12 discrete edges.
- [ ] **IMU / PIPA / gyro torquing.**
      *Verification:* gyro pulse counts per channel-14 command.
- [ ] **Uplink (UPRUPT) and downlink (DOWNRUPT) telemetry.**
      *Verification:* a known uplink word arriving in INLINK and raising UPRUPT
      at the right MCT.
- [ ] **Radar (RNRAD, RADARRUPT).**
      *Verification:* probe.

## Phase 4 — Frontends and content

- [ ] **SDL frontend** — a real DSKY: seven-segment display, keypad, lamps,
      paced from a core-side output ring rather than a wall-clock timer, plus a
      `--frames` bounded mode so it can be smoke-tested headlessly.
      *Verification:* boots a rope and displays V37 under a dummy SDL driver in
      CI.
- [ ] **Run the Validation suite to completion and read its verdict** out of
      erasable memory rather than merely not alarming.
      *Verification:* every `Validate<OP>` module reports pass; this is the
      single highest-value item in the whole plan.
- [ ] **Boot the physical rope-module dumps** in `bios/rope-modules/`, including
      the `-BadBits` and `-SomeDefects` variants.
      *Verification:* the defective modules raise PARITY FAIL and the
      `-Repaired` controls do not — the alarm proving itself on real damaged
      hardware.
- [ ] **Characterise Aurora 12's RUPT LOCK** (PROJECT_STATUS known gap).
      *Verification:* either a FINDINGS row explaining why it is correct
      behaviour for that rope, or a fix with the probe that proves it.
- [ ] **Rope-module dump loader coverage**: dumps are bank pairs, not whole
      ropes, and load at a bank offset.
      *Verification:* assembling Luminary 131 from source and diffing against
      `Luminary131PlusLM131R1ModuleDump.bin` — a free end-to-end check of the
      loader against real hardware.

## Phase 5 — Beyond Block II

Explicitly deferred, listed so the plan names everything:

- [ ] Block I. A genuinely different machine (different word length usage,
      different instruction set, different pulse sequences). Out of scope until
      Block II is finished; `ext/agcplusplus` models it if it becomes wanted.
- [ ] A verified fast mode (guide §1). **Not needed:** the reference core
      already runs ~6.7× real time on a modern host, and the guide's own advice
      is not to compromise the reference core for speed that is not required.
      Revisit only if a full mission-duration run becomes a goal.

---

## Tails discovered while implementing

Recorded as found, per the working conventions.

- [ ] `agc_format_state` is the de facto golden format. It needs to be frozen
      and documented before the first golden is checked in, or every future
      field addition breaks the regression.
- [ ] `AGC_CHANNEL_COUNT` is 64; the DSKY protocol uses fictitious channel 0163
      for lamp state. Decide whether that lives in the core or the frontend
      before the DSKY lands.
- [ ] The unimplemented-subinstruction fallback in `cpu.c` silently substitutes
      STD2. Once the probe suite exists it should be a hard failure instead —
      every reachable (SQ, ST, EXTEND) triple is in the table, so hitting it is
      a bug in us.
- [ ] `tools/build_ropes.sh` builds yaYUL by globbing its sources; a Virtual AGC
      bump that adds a `main`-bearing file would break it. Pin or filter.
