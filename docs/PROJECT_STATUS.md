# Project status

The single source of truth for **what works and what backs it**. Updated in the
same commit as the code it describes.

Last updated: 2026-07-25.

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

**Divide results are verified against a runnable oracle**, on 13 of 13 cases.
That check was worth building: divide was silently wrong for the project's
entire life, losing the top bit of every quotient, and it took exactly this to
find it (FINDINGS #40). Unit tests passed, all 26 timings matched the memo — the
divide took precisely the right number of pulses while computing the wrong
number — and five flight ropes booted for 2.34 emulated seconds without an
alarm.

What is **not** yet claimed: the pulse-level interleaving within an MCT is still
unconfirmed for the two sequences where we follow AGCPlusPlus over the memo
without gate-level backing (FINDINGS #9, #11); only MP and DV have been checked
against the oracle for *results*; and no probe yet reads the Validation rope's
own pass/fail verdict, so correctness elsewhere rests on unit tests rather than
on MIT's suite.

## Subsystems

| Subsystem | State | Verification |
|---|---|---|
| Sequence generator (SQ/ST/EXTEND decode, T1–T12 dispatch, BR conditions) | Working | `cpu_suite`; tables re-derived and diffed against the memo by `subinst_tables_are_current` in CTest |
| Control pulses (all 71, plus the three implicit signals) | Working | Exercised through the subinstruction tables by `cpu_suite`; individual semantics read from the memo |
| Central registers + adder (ones' complement, end-around carry, NEACON) | Working | `cpu_suite`: end-around carry, x + (−x) = −0 |
| Erasable memory (destructive read at T5, rewrite before T10, editing registers) | Working | `memory_suite` |
| Fixed memory (rope layout, parity, bank + superbank addressing) | Working | `memory_suite`; parity alarm confirmed by `cpu_suite` |
| Banking (EB, FB, BB, FEXT) | Working | `memory_suite`; flight ropes switch banks continuously without alarming |
| Scaler / timer (17 stages, documented taps) | Working | `timing_suite`: divisor, stage-10 rising edge → TIME1/TIME3, stage-6 → TIME6 gated on channel 13 bit 16; F05A drive counters via the `counters` probe |
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
| Probe framework (`tools/probes/asm.py`, sentinels, `regress.py`) | Working | `probe_regression` in CTest, both build types |
| Instruction-timing verification | Working | `timing` probe: 26 instructions asserted against the memo |
| Counter-interference verification | Working | `counters` probe: 31 whole MCTs stolen, invariants asserted |
| Branch asymmetry verification | Working | `branches` probe: 8 cases, taken 1 MCT vs not-taken 2, both zeroes |
| Interrupt discipline verification | Working | `interrupts` probe: refusal on A overflow (with a control), RESUME |
| Mid-instruction integrity verification | Working | `integrity` probe: MP/DV round trips give the same right answer under counter traffic |
| Runnable oracle | Working | `tools/oracle/build_oracle.sh`; pulse-by-pulse diff against ours |
| Instruction-set differential test | Working | `oracle_differential` in CTest; 2496/2496 cases agree over 21 instructions |
| Long-run rope state hashes | Working | `rope_state_hashes` in CTest; 9 ropes, identical on both build types |
| Probes for the remaining emergent behaviour | **Partial** | See gaps |

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
- **Aurora 12's RUPT LOCK is uncharacterised.** It may be correct behaviour for
  a development rope that never arms an interrupt source, or it may be the
  missing SHINC path. Characterise before fixing.
- **FINDINGS.md rows 9, 10, 11, 13 are open**: places where AGCPlusPlus differs
  from the memo and we have followed it without a gate-level confirmation.
