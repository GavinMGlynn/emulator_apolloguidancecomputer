# Project status

The single source of truth for **what works and what backs it**. Updated in the
same commit as the code it describes.

Last updated: 2026-07-25.

## Accuracy claim

Not yet earned. What can be said today:

- The core is stepped **once per timing pulse** — 1.024 MHz, twelve pulses to
  the 11.71875 µs MCT — and executes the control pulses that AGC4 Memo #9
  assigns to each pulse of each subinstruction. There is no instruction-level
  path through `src/core`.
- All 57 Block II subinstructions are present, transcribed mechanically from
  the memo as corrected by `ext/agcplusplus` (`tools/gen_subinst_tables.py`,
  re-checked by CTest so the committed tables cannot drift).
- Results are bit-identical between the `-O0` and `-O3 -flto` builds across
  nine flight and test ropes run for 200 000 MCTs each.

What is **not** yet claimed: that any individual timing number has been checked
against a self-measuring probe. There is no probe suite and no golden
regression yet — that is the next phase, and until it exists "cycle-correct"
describes the *construction* of the core, not a verified property of it.

## Subsystems

| Subsystem | State | Verification |
|---|---|---|
| Sequence generator (SQ/ST/EXTEND decode, T1–T12 dispatch, BR conditions) | Working | `cpu_suite`; tables re-derived and diffed against the memo by `subinst_tables_are_current` in CTest |
| Control pulses (all 71, plus the three implicit signals) | Working | Exercised through the subinstruction tables by `cpu_suite`; individual semantics read from the memo |
| Central registers + adder (ones' complement, end-around carry, NEACON) | Working | `cpu_suite`: end-around carry, x + (−x) = −0 |
| Erasable memory (destructive read at T5, rewrite before T10, editing registers) | Working | `memory_suite` |
| Fixed memory (rope layout, parity, bank + superbank addressing) | Working | `memory_suite`; parity alarm confirmed by `cpu_suite` |
| Banking (EB, FB, BB, FEXT) | Working | `memory_suite`; flight ropes switch banks continuously without alarming |
| Scaler / timer (17 stages, documented taps) | Working | `timing_suite`: divisor, stage-10 rising edge → TIME1/TIME3, stage-6 → TIME6 gated on channel 13 bit 16 |
| Hardware alarms (PARITY FAIL, TC TRAP, RUPT LOCK, NIGHT WATCHMAN) | Working | `timing_suite`, one test per alarm, isolated with `alarm_inhibit` |
| Priority control — counters (PINC, MINC, PCDU, MCDU, DINC) | Working | `timing_suite`: MCT stealing, address-order priority, TIME1→TIME2 carry, TIME3→T3RUPT |
| Priority control — interrupts (vectoring, KRPT, RESUME, no nesting) | Working | Exercised by every rope boot; `cpu_suite` covers INHINT/RELINT |
| Priority control — serial counters (SHINC, SHANC) | **Missing** | See gaps |
| I/O channels (aliasing, inverted channels, edge detection) | Working, partial | `timing_suite` uses channel 13; no peripheral consumes the outputs yet |
| DSKY | **Missing** | — |
| CDU / IMU / gyro / PIPA | **Missing** | — |
| Uplink / downlink / radar | **Missing** | — |
| Headless frontend | Working | Used for every rope boot above |
| SDL frontend | **Missing** | — |
| Probe suite + golden regression | **Missing** | The next phase |

## Software that runs

Nine ropes assembled from the original listings by `tools/build_ropes.sh`, each
run for 200 000 MCTs (2.34 emulated seconds) from a cold GOJAM:

| Rope | Result |
|---|---|
| Luminary 099 (Apollo 11 LM) | runs, no alarm |
| Comanche 055 (Apollo 11 CM) | runs, no alarm |
| Luminary 131 (Apollo 13/14 LM) | runs, no alarm |
| Artemis 072 (Apollo 15–17 CM) | runs, no alarm |
| Zerlina 56 | runs, no alarm |
| Validation (MIT instruction validation suite) | runs, no alarm |
| Retread 50 | runs, no alarm |
| Sundial E | runs, no alarm |
| Aurora 12 | **RUPT LOCK** between 20 000 and 60 000 MCTs — uncharacterised |

"No alarm" means the machine did not restart itself. It does **not** yet mean
the ropes are computing correct answers: nothing reads the Validation suite's
own pass/fail cells yet, and there is no DSKY to display anything. Boots are
thermometers, not milestones.

Performance, release build, this host: 200 000 MCTs in ~0.35 s, i.e. roughly
6.7× real time for a strictly per-timing-pulse interpreter. No fast mode is
needed or planned yet.

## Deliberate approximations

Each has a reason and a cost to close, and each is a named item in
`docs/COMPLETION_PLAN.md`.

| Approximation | Reason | Cost to close |
|---|---|---|
| **SHINC/SHANC counter requests are dropped, not serviced.** | The serial shift registers (uplink, radar) do not exist, and leaving a request pending would deadlock priority control. | Small once the uplink/radar registers exist; the sequences themselves are already in the tables. |
| **POUT and MOUT do nothing.** | They drive the CDU error counters and gyro torque pulses; there is no CDU or IMU. Marked PROVISIONAL in `pulses.c`. | Medium — needs the CDU subsystem. |
| **Channel 30's discretes are frozen** at "TEMP IN LIMITS, IMU OPERATE" and channels 31–33 at all-ones. | No spacecraft to drive them. Documented in `channels.c`. | Small per discrete, once there is something to model. |
| **Fixed memory covers the full 40 960-word superbank span**, with only 36 864 populated. | An out-of-range fetch then reads zeroes and fails parity, which is what the hardware does when the sense lines find no rope. Not an approximation so much as a deliberate choice; recorded so it is not mistaken for a bug. | n/a |
| **No wall-clock pacing anywhere in the core.** | Determinism. Time advances only when the frontend calls `agc_tick`. | n/a — this is a design rule, not a gap. |

## Known gaps that are not approximations

- **No probe suite.** Every timing claim above is structural ("the table says
  so"), not measured. Until self-measuring probes exist and their outputs are
  locked in as goldens, a regression in a pulse sequence would only be caught
  if it happened to break a unit test or alarm a rope.
- **Aurora 12's RUPT LOCK is uncharacterised.** It may be correct behaviour for
  a development rope that never arms an interrupt source, or it may be the
  missing SHINC path. Characterise before fixing.
- **FINDINGS.md rows 9, 10, 11, 13 are open**: places where AGCPlusPlus differs
  from the memo and we have followed it without a gate-level confirmation.
