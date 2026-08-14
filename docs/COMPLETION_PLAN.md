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
      *Verified:* **green on all four**, as of the "Fix the Rocky CI job"
      commit. It was red on every push before that, and only ever on the Rocky
      container job: `rockylinux:9` has no git, so `actions/checkout` fell back
      to a REST tarball with no `.git` and the submodule step three lines later
      died with "not a git repository". Installing git *before* the checkout is
      the fix, and is the trap §3 of the guide documents by name — we had the
      advice and walked into it anyway.
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

## Phase 2 — Verification (in progress)

This is what turns "cycle-correct by construction" into a checkable claim.

- [x] **Bare-metal probe framework.** `tools/probes/asm.py` is a ~200-line
      Block II assembler emitting rope images directly, no toolchain involved.
      A probe signals moments by storing into watched erasable cells;
      `agc_headless --sentinel` records the emulated timing pulse.
      *Verified:* `probe_regression` in CTest. The reason the harness times the
      probe rather than the probe timing itself — the AGC has no fine-grained
      software-readable counter — is written up in `tools/probes/README.md`.
- [x] **Instruction timing measured against the memo.** The `timing` probe
      brackets 26 instructions between sentinel stores.
      *Verified:* every one matches AGC4 Memo #9's sequence tables, asserted
      rather than merely frozen. FINDINGS #25-30.
- [x] **Probes for the emergent behaviour** unit tests cannot reach.
      *Done:* the `counters` probe — a peripheral steals 31 whole MCTs from a
      program that never interacted with it (FINDINGS #31). The `branches`
      probe — taken/not-taken asymmetry, and both ones'-complement zeroes
      branching (#32-34). The `interrupts` probe — refusal while A holds
      overflow, with a control, and RESUME restoring the program (#35-37).
      The `integrity` probe — 40 multiply/divide round trips return the same
      right answer quiet and under counter traffic, so a steal never splits an
      instruction (#40-41).
      The `counters` probe also raises a full storm: six channel-14 drive
      counters at 3.2 kHz cost a third of the machine (#44-45).
      The `peripherals` probe closes it, and is the only one that measures the
      machine against the *outside*: a word is uplinked from the ground while
      the program runs, arriving one bit per 156 microseconds and stealing
      sixteen whole MCTs to shift itself into INLINK, and the program reports
      what it received. If the word comes out right the whole chain came out
      right — the Inlink Control's gating, the request cells, priority control's
      arbitration, both shift sequences, the flag detection and UPRUPT — and it
      counts its own loop passes to prove it was running throughout rather than
      stopped in one long instruction. The DSKY rides along in the same run,
      with the decoded panel in the golden.
- [x] **Golden regression.** `tools/regress.py` wired into CTest, running every
      probe and diffing its output against a checked-in golden.
      *Verified:* goldens identical on `-O0` and `-O3 -flto`, **and now on all
      four CI platforms** — which is the point of them: the numbers are emulated
      timing pulses, so a platform where one differs has a real bug.
- [x] **Differential-test the instruction set against the oracle.**
      `tools/oracle/differential.py` sweeps operand values chosen for the
      awkward cases and compares full 16-bit registers, not just memory.
      *Verified:* 2496/2496 agree over 21 instructions (FINDINGS #42); a quick
      sweep runs as the `oracle_differential` CTest, skipping cleanly when
      ext/agcplusplus is not initialised.
      *Tail:* INDEX, the branches, the channel instructions and RESUME have no
      simple operand sweep and are not covered.
- [x] **Long-run state hashes** for each rope at fixed MCT counts, as the
      identity harness for any future optimization. `tools/hash_ropes.py`
      hashes the whole of erasable memory, the register file and the counter
      cells after 200 000 MCTs.
      *Verified:* 10 ropes, byte-identical between `-O0` and `-O3 -flto`, as the
      `rope_state_hashes` CTest. Skips cleanly when `roms/` is empty, which is
      what CI does — there are no ropes there, so this one stays developer-side
      by design.
- [x] **Build a runnable oracle.** `tools/oracle/build_oracle.sh` links the
      Block II core out of `ext/agcplusplus` with no sockets, DSKY or threads,
      and `ORACLE_TRACE=1` logs one line per timing pulse for a direct diff
      against our `--trace`.
      *Verified:* it found the divide defect in FINDINGS #40, and settled two of
      the four open memo divergences (#10, #13).
- [x] **Read the gate-level oracle** (`ext/agc_simulation`) to settle the two
      rows still open in `tools/oracle/FINDINGS.md` (#9 DAS1 T10/T11, #11 MP3
      T6/T12). Both closed, and #11 turned out to be a real defect: the memo's
      NEACOF placement is right and the end-around carry is held off for the
      rest of MP3 by a second term, `MP3A`, that no document mentions.
      `tools/oracle/gate_sim.py` evaluates the netlists directly (no Verilog
      toolchain); `gate_crosspoint.py` prints a timeline; `gate_diff.py` sweeps
      the whole table.
      *Verified:* the captured timelines are in FINDINGS #47-50, and
      `gate_level_crosspoint` in CTest checks all 1392 rows on every build —
      29 subinstructions x 4 branch values x 12 timing pulses, 0 disagreements.
      Detail in `PROJECT_STATUS.md`.

## Phase 3 — Peripherals

- [x] **DSKY.** Channel 10 relay words (thirteen banks of latching relays, not a
      decoded display), channel 11 lamps, relay bank 14's status lights,
      channels 15/16 keyboard input, KEYRUPT1/2, and the flash — which turns out
      not to be software at all but `NOR(FS17, FS16)` off the scaler, gating the
      VERB/NOUN displays in antiphase with the KEY REL and OPR ERR lamps.
      *Verified:* `dsky_suite` (19 tests) pins every encoding against the source
      that settled it, and **Sundial E displays `VERB 05 NOUN 31` with `01107`
      in R2** — matching its own listing's `FAILDISP OCT 00531` and the comment
      on alarm code 1107, "WILL BE DISPLAYED IN R2". `--press KEY:MCT` drives
      the keyboard from the headless frontend and `--dump-dsky` / `--trace-dsky`
      read the panel. Detail in `PROJECT_STATUS.md`; encodings and sources in
      FINDINGS #54-60.
      *Tails:* the PRO/STBY key and the hardware-driven RESTART and STBY lamps
      are not modelled (they are start-stop logic, not channel traffic); neither
      is the second DSKY, which sees identical relay words.
- [x] **Serial counters SHINC/SHANC**, closing the dropped-request
      approximation, plus the Inlink Control that drives them: channel 13's two
      gating bits, the cabin block switch, the 156 microsecond rate limit and
      its channel 33 "uplink too fast" bit. `ext/agcplusplus` has neither
      sequence, so both come from AGC4 Memo #9 via the generator's own parse of
      it.
      *Verified:* `uplink_suite` (12 tests) shifts known words in and reads them
      back, pins the flag-bit/UPRUPT rule and the rate limit, and measures the
      sixteen MCTs a word steals from the program. **Luminary 099 acts on an
      uplinked word**: four triple-redundant key codes raise UPRUPT and the
      rope's own handler lights UPLINK ACTY. `--uplink KEY:MCT` sends from the
      headless frontend. FINDINGS #61-66.
      *Tails:* no crosslink partner is modelled, so a program that selects it
      (channel 13 bit 5) stops hearing the uplink; and nothing yet shows a rope
      acting on an uplinked word's *content* — see the display tail below.
- [x] **CDU** — the five angle counters, the drive counters, channel 12's zero
      and enable discretes, and the drive path. **Closes the POUT/MOUT
      approximation**: those two pulses now emit drive pulses on the axis whose
      counter DINC is addressing, instead of doing nothing.
      *Verified:* `cdu_suite` (11 tests) — angle counters stepped by PCDU/MCDU
      through both ones'-complement zeroes, pulses lost when one arrives before
      the last is serviced, the zero discretes stopping the converter without
      touching the counters, a loaded drive counter walking to minus zero while
      emitting exactly its count in pulses, and ZOUT taking each axis's own bit
      out of channel 14 as it finishes. Found a real defect on the way:
      FINDINGS #71, ZOUT could never stop the X drive.
      Coarse align was the one piece left when this landed, because acting on it
      means moving a gimbal; the IMU item below closed it.
- [x] **IMU / PIPA / gyro torquing.**
      *Done:* gyro torquing — the program loads GYROD, picks an axis and a sign
      in channel 14 and the hardware walks the counter down at 3.2 kHz, one
      torque pulse per DINC; the three PIPAs, one PINC/MINC per velocity
      increment; and coarse align, which is the one path where the computer
      moves the platform rather than measuring it — the drive pulses reach the
      gimbal and the CDU reports the movement back, so the AGC's own angle
      counters follow what it commanded.
      *Verified:* `imu_suite` (10 tests) — gyro pulse counts per channel-14
      command, table 30-5C's whole selection truth table including "none", the
      sign bit, PIPA increments in both directions, a lost velocity increment,
      and coarse align with a control that shows the platform staying put
      without it.
      And the check this item named: **a known attitude commanded through coarse
      align and read back** — a hundred CDU counts on each of the three gimbals,
      driven out at the hardware's own rate, with the machine's angle counters
      ending on exactly what it asked for and each axis taking its own bit back
      out of channel 14 as it finishes.
      *Not modelled, deliberately:* vehicle dynamics. Nothing integrates an
      acceleration, moves a gimbal at a rate or drifts a gyro. That is
      spacecraft simulation rather than computer emulation — the core's job is
      to turn motion into the pulses the hardware would send, and the AGC cannot
      tell the difference because pulses are all it ever sees. Recorded as an
      approximation in `PROJECT_STATUS.md`, and it belongs to a frontend if a
      closed-loop flight ever becomes a goal.
- [x] **Uplink (UPRUPT) and downlink (DOWNRUPT) telemetry.**
      *Done:* the uplink, above — a known word arrives in INLINK and raises
      UPRUPT when its flag bit shifts out.
      *Done:* the downlink too, and it turned out not to shift at all — the
      Downlink Converter reads channels 34 and 35 whole and serialises them
      itself, so DOWNRUPT is the ground station asking for the next word rather
      than anything the AGC shifted. OUTLNK is the shift-out path and raises no
      interrupt; wiring it to DOWNRUPT, as this once did, would have had the
      computer interrupted by its own crosslink.
      *Verified:* `telemetry_suite`.
- [x] **Radar (RNRAD, RADARRUPT).** Channel 13's mode selection — where two of
      the eight combinations are deliberately "none" — and the same
      flag-and-fifteen-bits protocol as the uplink into a different counter and
      a different interrupt.
      *Verified:* `telemetry_suite`: the selection table, both "none" cases, and
      a word assembling in RNRAD and raising RADARRUPT and not UPRUPT.
      *Tail:* nothing models a radar, so the answer's *content* is whatever a
      frontend supplies; and counter ALT (0060), the LM altitude meter's
      shift-out, is not implemented — our counter table stops at OUTLNK.

## Phase 4 — Frontends and content

- [x] **SDL frontend** — a real DSKY on SDL3: seven-segment digits drawn as
      segments (no font, no assets), the three-segment signs, the lamps, and a
      keypad mapped to the keyboard including PROCEED. `--frames N` bounds a run
      and `--screenshot PATH` writes the panel to a PNG through libpng, which is
      how a headless run gets verified on the real output rather than a proxy.
      *Verified:* `dsky_frontend` in CTest runs it under SDL's dummy video
      driver and checks the panel matches what the headless frontend reads off
      the same rope — **Sundial E's `VERB 05 NOUN 31` with `01107` in R2**, not
      V37: no flight rope reaches a display from a cold start (see the tail
      below), so the plan's original wording named a check that could not pass.
      *Pacing:* on emulated time rather than a wall clock or an audio queue —
      the AGC has no audio, and determinism stays in the headless frontend,
      which is what the goldens describe.
      *Tail:* built only where SDL3 is present, and not vendored as a submodule;
      the four-platform CI matrix will skip it until it is.
- [x] **Run the Validation suite to completion and read its verdict.** It turns
      out not to write a pass/fail cell at all: it reports through the DSKY, the
      way it would report to a technician standing in front of one — a code in
      PROG, a sub-code in NOUN, OPR ERR lit, waiting for PRO. So reading the
      verdict meant implementing PROCEED (channel 32 bit 14, low polarity) and
      an `--auto-proceed` mode that presses it at every stop and reports what
      was displayed.
      *Verified:* **it passes.** `mit_validation_suite` in CTest runs a full
      pass — about 3.37 million MCTs, 39 seconds of emulated time — and checks
      that the only codes displayed are the opening checkpoint and the closing
      PROG 77 (MAXERR). No `Validate<OP>` module reports a failure. Detail in
      `PROJECT_STATUS.md`.
- [x] **Boot the physical rope-module dumps** in `bios/rope-modules/`.
      *Verified:* `physical_rope_modules` in CTest. Booting Retread 50 from the
      **defective** B1 raises **PARITY FAIL** (channel 77 bit 1); booting it
      from the same module **repaired** does not. The alarm proving itself
      against damage nobody simulated. All thirteen Block II dumps are also
      parity-scanned: the BadBits article has 702 words failing odd parity and
      every other one is clean.
- [x] **Characterise Aurora 12's RUPT LOCK.** It is our own frozen input
      discretes, and the rope is behaving as written: channel 32 idles at all
      ones, Aurora complements that to "nothing has failed", and its
      highest-set-bit loop (`DOUBLE` / `TS` / `TCF`) has no exit for +0.
      *Verified:* hold any one of channel 32's low eight bits low and Aurora
      runs 200 000 MCTs with no alarm; leave it idle and it restarts at MCT
      32 427 every time. Not an arithmetic defect — the loop turns on DOUBLE,
      TS's overflow skip and MP by +0, all three of which MIT's Validation
      suite exercises and passes. FINDINGS #72-73.
- [x] **Rope-module dump loader coverage.** The premise was wrong twice over
      and both corrections are in `agc_memory_load_module`: a Block II module is
      **six banks**, not a bank pair; the dumps are in yaAGC's `--parity` word
      layout rather than the `--hardware` layout our fixed memory stores; and
      the first four banks of a rope image are ordered **02, 03, 00, 01**. The
      named comparison was also against the wrong rope — that dump is of
      **LM131R1**, which `tools/build_ropes.sh` now assembles.
      *Verified:* LM131R1 assembled from source equals the dump taken off the
      physical article in **all 36 864 words**, which exercises the assembler
      invocation, the bit layout, the parity rule and the bank order at once.
      FINDINGS #74-75.

## Phase 5 — Beyond Block II

Explicitly deferred, listed so the plan names everything:

- [ ] Block I. A genuinely different machine (different word length usage,
      different instruction set, different pulse sequences). Out of scope until
      Block II is finished; `ext/agcplusplus` models it if it becomes wanted.
- [x] A verified fast mode (guide §1). **Done as the guide's other half:**
      squeeze the reference core under the identity harness rather than build a
      second one. Exact-skip scheduling turns out to have nothing to offer this
      machine — the AGC's CPU executes a subinstruction every MCT, so there is
      no inert span to skip across (FINDINGS #77) — while dispatch overhead had
      a great deal.
      *Verified:* **1.37× and byte-identical**, with the probe goldens and all
      ten rope state hashes unchanged; about **79× real time**, from 58. The
      profiler found 39% of run time going to `strcmp` in the TC TRAP check, and
      two optimizations that looked obvious measured worse or not at all and
      were reverted. FINDINGS #77-79.

---

## Tails discovered while implementing

Recorded as found, per the working conventions.

- [x] `agc_format_state` is the de facto golden format. **Resolved differently:**
      the probe goldens hold `SENT` lines and memory dumps, not the state line,
      so a field added to the trace format does not break the regression.
- [ ] `--sentinel` fires on "first non-zero", so a probe cannot mark a moment
      with a zero value. Fine for every probe so far; document or widen it
      before one needs to.
- [x] `AGC_CHANNEL_COUNT` is 64; the DSKY protocol uses fictitious channel 0163
      for lamp state. **Resolved: the frontend's problem, not the core's.**
      0163 is an invention of yaAGC's socket protocol, used to carry the lamps
      the DSKY itself latches or flashes (KEY REL, OPR ERR, RESTART, STBY)
      rather than anything the AGC addresses. The core exposes `agc_dsky` state
      structurally — including the flash phase — so a frontend speaking that
      protocol can synthesise 0163 without a fictitious address existing inside
      the machine.
- [ ] The unimplemented-subinstruction fallback in `cpu.c` silently substitutes
      STD2. Once the probe suite exists it should be a hard failure instead —
      every reachable (SQ, ST, EXTEND) triple is in the table, so hitting it is
      a bug in us.
- [ ] `tools/build_ropes.sh` builds yaYUL by globbing its sources; a Virtual AGC
      bump that adds a `main`-bearing file would break it. Pin or filter.
- [ ] **TCSAJ3 is not implemented.** The memo lists it and the gates decode it;
      it is the Computer Test Set's "transfer control to specified address jam",
      so nothing in flight reaches it and `ext/agcplusplus` never modelled it
      either. Named in `gate_diff.py` as `NOT_MODELLED` so its absence stays a
      decision. Close it if the CTS interface is ever modelled.
- [ ] **The gate sweep does not cover the divide sequences.** The grey counter in
      module A4 free-runs when no divide is in progress, so the bench holds the
      divide conditions quiet and probes only the non-divide subinstructions
      (FINDINGS #53). Covering DV0-DV7 needs that counter driven stage by stage.
      *Verification:* the same 12-pulse timeline per divide stage, diffed against
      `subinst_tables.c` like the other 29.
- [ ] **The flight ropes reach no display from a cold start.** Luminary 099,
      Comanche 055, Artemis 072 and Zerlina 56 cycle every relay bank through
      DSPOUT and write blanks, with the PROG light on; keypresses do reach
      `CHARIN` (pressing VERB lights KEY REL, its documented response). Sundial E
      and Validation both display. Characterise before assuming it is ours —
      FINDINGS #59.
      *Verification:* either a listing-backed explanation of what these ropes
      wait for, or the fix with the probe that proves it.
- [ ] **`mp3a` during a stolen MCT is unverified.** MP3A is a decode line, so a
      counter sequence servicing a request between MP1 and MP3 runs with the
      end-around carry still inhibited (FINDINGS #49). That falls out of the
      netlist and matches it, but nothing else confirms it.
      *Verification:* a probe that forces a counter request into that window and
      reads the counter, or the gate-level model run for those MCTs.
