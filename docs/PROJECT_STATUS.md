# Project status

The single source of truth for **what works and what backs it**. Updated in the
same commit as the code it describes.

Last updated: 2026-08-15.

## Accuracy claim

**Instruction timing is verified.** All 26 instructions the `timing` probe
measures come out at exactly the MCT counts AGC4 Memo #9's sequence tables
predict — checked as an assertion in CTest, not merely frozen in a golden. That
includes divide, which had to be measured before it could be asserted: DVST
lets a divide sub-sequence end at T3 instead of T12, so a divide is built from
unequal segments, and the fact that it totals exactly 72 timing pulses was a
result rather than an assumption.

The rest of what can be said today:

- The core is stepped **once per timing pulse** — 1.024 MHz, twelve pulses to
  the 11.71875 µs MCT — and executes the control pulses that the memo assigns
  to each pulse of each subinstruction. There is no instruction-level path
  through `src/core`.
- All 57 Block II subinstructions are present, transcribed mechanically from
  the memo as corrected by `ext/agcplusplus` (`tools/gen_subinst_tables.py`,
  re-checked by CTest so the committed tables cannot drift).
- Counter interference is verified as *emergent*: the `counters` probe measures
  a peripheral stealing 31 whole MCTs from a program that never interacted with
  it.
- Results are bit-identical between the `-O0` and `-O3 -flto` builds — across
  the probe goldens and across nine flight and test ropes run for 200 000 MCTs
  each.

**MIT's own instruction validation suite passes.** The Validation rope runs to
completion on our core and reports success on the panel — 3.37 million MCTs, 39
seconds of emulated time, and not one of its `Validate<OP>` checks fails. That
is the strongest statement available about whether this machine computes the
right answers, because it is MIT's own test of the instruction set rather than
ours, and `mit_validation_suite` in CTest asserts it on every run.

Reading the verdict took implementing the way the suite actually reports.
It writes no pass/fail cell: it puts a code in PROG and a sub-code in NOUN,
lights OPR ERR and waits for the PRO key, exactly as it would report to a
technician standing at a DSKY. A pass is two stops and nothing between them —
the opening checkpoint (`INIT` calls the error display unconditionally, which
is why a Validation rope left alone looks like it is sitting on an error) and
the closing PROG 77, from MAXERR. Afterwards it sits in `DONE TCF DONE` and
**our TC TRAP alarm restarts the machine**, which is exactly what that alarm is
for and means a long run shows the suite running over and over.

**The pulse tables are verified against the gates.** Every subinstruction the
sequence generator can decode without a Computer Test Set — 29 of them, times
four branch-register values, times twelve timing pulses, 1392 rows — is read
straight out of the gate netlists in `ext/agc_simulation` and diffed against
`src/core/cpu/subinst_tables.c` by the `gate_level_crosspoint` CTest. Zero
disagreements. Those netlists are generated from the original MIT logic flow
diagrams, so this is the first check in the project that answers to hardware
rather than to another model. It runs in about seven seconds and needs no
Verilog toolchain: `tools/oracle/gate_sim.py` evaluates the netlist directly.

That check earned its keep immediately by finding a defect in multiply that
three other harnesses could not (FINDINGS #48-50). AGC4 Memo #9 fires NEACOF at
MP3 T6; `ext/agcplusplus` moves it to T12 to keep the end-around carry inhibited
through the multiply's final sum. The gates do neither: they clear the NEAC latch
at T6 — NEACOF *is* TL15, the pulse that copies L15 into BR1 — and hold the carry
off for the rest of MP3 with a **separate `MP3A` term on the carry gate that no
document mentions** (`CINORM = NOR(NEAC, EAC, MP3A)`, module A7 gate 33457). We
now model the term, which puts us closer to the hardware than the oracle is.
Without it, `MP` is wrong for 8 of 100 operand pairs — every product whose high
half lands on -0. Multiply had no unit test at all until this; it does now.

**Divide results are verified against a runnable oracle**, on 13 of 13 cases.
That check was worth building: divide was silently wrong for the project's
entire life, losing the top bit of every quotient, and it took exactly this to
find it (FINDINGS #40). Unit tests passed, all 26 timings matched the memo — the
divide took precisely the right number of pulses while computing the wrong
number — and five flight ropes booted for 2.34 emulated seconds without an
alarm.

What is **not** yet claimed: the gate sweep does not cover the divide sequences,
because module A4's grey counter free-runs when no divide is in progress and its
state would leak into the reading (FINDINGS #53) — DV is still backed only by the
memo, the model and the oracle's 13/13 results. Nor does it cover the involuntary
counter sequences. And no probe yet reads the Validation rope's own pass/fail
verdict, so correctness elsewhere rests on unit tests rather than on MIT's suite.

## Subsystems

| Subsystem | State | Verification |
|---|---|---|
| Sequence generator (SQ/ST/EXTEND decode, T1–T12 dispatch, BR conditions) | Working | `cpu_suite`; tables re-derived and diffed against the memo by `subinst_tables_are_current`, and against the gate netlists by `gate_level_crosspoint` |
| Control pulses (all 71, plus the three implicit signals) | Working | Exercised through the subinstruction tables by `cpu_suite`; individual semantics read from the memo |
| Central registers + adder (ones' complement, end-around carry, NEACON + MP3A) | Working | `cpu_suite`: end-around carry, x + (−x) = −0, and multiply's two carry inhibits |
| Erasable memory (destructive read at T5, rewrite before T10, editing registers) | Working | `memory_suite` |
| Fixed memory (rope layout, parity, bank + superbank addressing) | Working | `memory_suite`; parity alarm confirmed by `cpu_suite` |
| Banking (EB, FB, BB, FEXT) | Working | `memory_suite`; flight ropes switch banks continuously without alarming |
| Scaler / timer (17 stages, documented taps) | Working | `timing_suite`: divisor, stage-10 rising edge → TIME1/TIME3, stage-6 → TIME6 gated on channel 13 bit 16; F05A drive counters via the `counters` probe |
| Hardware alarms (PARITY FAIL, TC TRAP, RUPT LOCK, NIGHT WATCHMAN) | Working | `timing_suite`, one test per alarm, isolated with `alarm_inhibit` |
| Priority control — counters (PINC, MINC, PCDU, MCDU, DINC) | Working | `timing_suite`: MCT stealing, address-order priority, TIME1→TIME2 carry, TIME3→T3RUPT |
| Priority control — interrupts (vectoring, KRPT, RESUME, no nesting) | Working | Exercised by every rope boot; `cpu_suite` covers INHINT/RELINT |
| Priority control — serial counters (SHINC, SHANC) | Working | `uplink_suite`; sequences generated from AGC4 Memo #9, which is the only source — the reference model has neither |
| Uplink / Inlink Control (channel 13 gating, rate limit, UPRUPT) | Working | `uplink_suite`; Luminary 099 raises UPRUPT on an uplinked word and lights UPLINK ACTY |
| I/O channels (aliasing, inverted channels, edge detection) | Working, partial | `timing_suite` uses channel 13; no peripheral consumes the outputs yet |
| DSKY — display (channel 10 relay banks, signs, status lights) | Working | `dsky_suite`; Sundial E's V05 N31 R2 01107 matches its own listing |
| DSKY — lamps and the flash (channel 11, scaler stages 16/17) | Working | `dsky_suite`: the flash runs without the program, and in antiphase with KEY REL |
| DSKY — keyboard (channels 15/16, KEYRUPT1/2) | Working | `dsky_suite`; a keypress into Luminary 099 lights KEY REL, `CHARIN`'s documented response |
| DSKY — PROCEED key (channel 32, low polarity) | Working | `mit_validation_suite` drives the whole Validation rope through it |
| DSKY — STBY key, RESTART and STBY lamps | **Missing** | Start-stop logic rather than channel traffic |
| CDU — angle counters, drive counters, channel 12 discretes | Working | `cdu_suite`; POUT/MOUT now drive, closing the approximation |
| CDU — coarse align | Working | `imu_suite`: the gimbal moves and the CDU reports it back, with a control |
| IMU — gyro torquing (channel 14 selection and sign) | Working | `imu_suite`; table 30-5C's selection truth table, including "none" |
| IMU — PIPAs | Working | `imu_suite`: velocity increments both ways, and one lost when it arrives too soon |
| IMU — dynamics (rates, drift, integration) | **Missing** | Deliberate: see approximations |
| Downlink converter (channels 34/35, DOWNRUPT) | Working | `telemetry_suite`; silent until a word rate is set, which is what a bench machine does |
| Outlink / crosslink shift-out (OUTLNK) | Working | `telemetry_suite`: every bit that leaves is transmitted, and it raises no interrupt |
| Radar (channel 13 selection, RNRAD, RADARRUPT) | Working | `telemetry_suite` |
| Altitude meter counter ALT (0060) | **Missing** | Our counter table stops at OUTLNK; LM only |
| Headless frontend | Working | Used for every rope boot above |
| SDL frontend | **Missing** | — |
| Probe framework (`tools/probes/asm.py`, sentinels, `regress.py`) | Working | `probe_regression` in CTest, both build types |
| Instruction-timing verification | Working | `timing` probe: 26 instructions asserted against the memo |
| Counter-interference verification | Working | `counters` probe: 31 whole MCTs stolen, invariants asserted |
| Branch asymmetry verification | Working | `branches` probe: 8 cases, taken 1 MCT vs not-taken 2, both zeroes |
| Interrupt discipline verification | Working | `interrupts` probe: refusal on A overflow (with a control), RESUME |
| Mid-instruction integrity verification | Working | `integrity` probe: MP/DV round trips give the same right answer under counter traffic |
| Runnable oracle | Working | `tools/oracle/build_oracle.sh`; pulse-by-pulse diff against ours |
| Instruction-set differential test | Working | `oracle_differential` in CTest; 2496/2496 cases agree over 21 instructions |
| Gate-level cross-point verification | Working | `gate_level_crosspoint` in CTest; 1392 rows over 29 subinstructions, 0 disagreements, read out of `ext/agc_simulation` |
| Multiply arithmetic | Working | `cpu_suite`: the double-precision product, the -0 high half, and both end-around-carry inhibits |
| Long-run rope state hashes | Working | `rope_state_hashes` in CTest; 9 ropes, identical on both build types |
| Probes for the remaining emergent behaviour | **Partial** | See gaps |

## Software that runs

Nine ropes assembled from the original listings by `tools/build_ropes.sh`, each
run for 200 000 MCTs (2.34 emulated seconds) from a cold GOJAM:

| Rope | Result | DSKY |
|---|---|---|
| Luminary 099 (Apollo 11 LM) | runs, no alarm | blank, PROG lit; drives every relay bank |
| Comanche 055 (Apollo 11 CM) | runs, no alarm | blank, PROG lit |
| Luminary 131 (Apollo 13/14 LM) | runs, no alarm | blank |
| Artemis 072 (Apollo 15–17 CM) | runs, no alarm | blank, PROG lit |
| Zerlina 56 | runs, no alarm | blank, PROG lit |
| **Validation (MIT instruction validation suite)** | **passes**, then restarts on TC TRAP from its own DONE loop | opening checkpoint, then `PROG 77` |
| Retread 50 | runs, no alarm | blank |
| **Sundial E** | runs, no alarm | **`VERB 05 NOUN 31`, `01107` in R2** |
| Aurora 12 | **RUPT LOCK** between 20 000 and 60 000 MCTs — uncharacterised | blank |

"No alarm" means the machine did not restart itself. It does **not** yet mean
the ropes are computing correct answers: nothing reads the Validation suite's
own pass/fail cells yet — except the Validation rope, which now does, and
passes. Boots are thermometers, not milestones.

Sundial E's display is the exception that carries real weight, because the rope
says what it should be showing: `ALARM_AND_ABORT.agc` displays `FAILDISP OCT
00531` — V05 N31 — and `FRESH_START_AND_RESTART.agc` sets alarm code `1107`
with the comment "WILL BE DISPLAYED IN R2". Bank assignment, digit codes and
register placement all check out at once, against the source of the program
doing the displaying. (The alarm is correct behaviour: a rope started cold has
no valid phase table.)

Performance, release build, this host: 200 000 MCTs in ~0.35 s, i.e. roughly
6.7× real time for a strictly per-timing-pulse interpreter. No fast mode is
needed or planned yet.

## Deliberate approximations

Each has a reason and a cost to close, and each is a named item in
`docs/COMPLETION_PLAN.md`.

| Approximation | Reason | Cost to close |
|---|---|---|
| **POUT and MOUT do nothing.** | They drive the CDU error counters and gyro torque pulses; there is no CDU or IMU. Marked PROVISIONAL in `pulses.c`. | Medium — needs the CDU subsystem. |
| **Channel 30's discretes are frozen** at "TEMP IN LIMITS, IMU OPERATE" and channels 31–33 at all-ones. | No spacecraft to drive them. Documented in `channels.c`. | Small per discrete, once there is something to model. |
| **Fixed memory covers the full 40 960-word superbank span**, with only 36 864 populated. | An out-of-range fetch then reads zeroes and fails parity, which is what the hardware does when the sense lines find no rope. Not an approximation so much as a deliberate choice; recorded so it is not mistaken for a bug. | n/a |
| **No wall-clock pacing anywhere in the core.** | Determinism. Time advances only when the frontend calls `agc_tick`. | n/a — this is a design rule, not a gap. |
| **Which half of the DSKY flash is lit is a guess.** | The gates fix the *rate* (`NOR(FS17, FS16)`) and the fact that the VERB/NOUN displays flash in antiphase to the KEY REL and OPR ERR lamps — both are asserted. Whether energising the VNFLSH relay blanks the display or lights it is a question about the panel's own wiring, and neither Information Series #30 nor the module sheets we hold say. We show the displays while FLASH is high. | Small, and cosmetic: it inverts a blink. Closing it needs a DSKY wiring diagram rather than an AGC one. |

## Known gaps that are not approximations

- **Probe coverage is thin.** Instruction timing, counter interference, branch
  asymmetry, interrupt discipline and mid-instruction integrity are covered.
  A counter storm is now probed too: six drive counters at 3.2 kHz take a third
  of the machine (FINDINGS #45).
- **Instruction results are now differential-tested** against the oracle:
  2496/2496 cases over 21 instructions, including both ones'-complement zeroes
  and the range extremes (FINDINGS #42). What that does *not* cover is the
  instructions with no simple operand sweep — INDEX, the branches, the channel
  instructions, RESUME — and it compares against a model rather than hardware.
- **The probes time from the harness, not from inside the machine.** The AGC has
  no software-readable fine-grained counter — its best is TIME6 at 53 MCTs per
  tick — so a probe signals moments and `--sentinel` records the emulated
  timing pulse. Exact and portable, but it means a probe could not be run
  against real hardware. Reasoning in `tools/probes/README.md`.
- **The flight ropes display nothing from a cold start.** Luminary 099,
  Comanche 055, Artemis 072 and Zerlina 56 all drive DSPOUT — every relay bank,
  208 words in 23 emulated seconds — and write blanks with the PROG light on.
  Sundial E and Validation both display. The keyboard path demonstrably reaches
  the software. Whether those ropes should get to a display unaided is a
  question about them rather than about the DSKY (FINDINGS #59), and it is a
  plan item, not a claim that this works.
- **Aurora 12's RUPT LOCK is uncharacterised.** It may be correct behaviour for
  a development rope that never arms an interrupt source, or it may be the
  missing SHINC path. **The second hypothesis is now eliminated**: the serial
  counters are implemented and Aurora 12 still latches the alarm at the same
  point. Characterise before fixing.
- **The gate reading is static, not a running simulation.** `gate_sim.py`
  evaluates the netlist to steady state per timing pulse, which is exactly right
  for a cross-point matrix (R-700 vol. III p. 5-6 calls it a logic product of
  time pulses and instruction codes) but says nothing about propagation delay,
  and it has no opinion about a latch whose set and reset terms are both quiet.
  Both limits are enforced rather than assumed: the simulator reports the nets
  that never settle and refuses to read a value from them.
- **All four FINDINGS rows that were open against the memo are now closed** (#9,
  #10, #11, #13). The remaining unverified corner is `mp3a` during an MCT stolen
  by priority control — faithful to the netlist, confirmed by nothing else.
