# Hardware references

Everything here is a primary source for the emulator. Where a number in the
core is not obvious, it cites one of these by name.

Most of these are committed. Three are not, and `./tools/fetch_docs.sh` restores
them (and anything else missing after a fresh clone); the script is idempotent
and skips files you already have.

| Not committed | Why |
|---|---|
| `agc-information-series/agcis_32_blk2_instructions.pdf` | 94 MB, within a few megabytes of GitHub's hard per-file limit. |
| `the-apollo-guidance-computer_jp2/` | 20 MB of page scans; fetched from archive.org. |
| `L-G-*.pdf` — Frank O'Brien, *The Apollo Guidance Computer: Architecture and Operation* | **The one thing here we may not redistribute.** Everything else is a government-funded MIT/Draper/NASA report, freely mirrored; O'Brien's is a current Springer/Praxis book. The fetch script does not pull it either. Buy it if you want it — it is the best modern narrative account of the machine, and useful background, but nothing in the core cites it. |

## Control pulses and instruction timing — the core of the project

| File | What it is | Why it matters |
|---|---|---|
| `AgcPulsesAndSequences.txt` | Hugh Blair-Smith's 2004 transcription of pages 30–50 of **AGC4 Memo #9**, "Block II Instructions" | **The primary source.** Defines all ~60 control pulses and tabulates, for every subinstruction, which pulses fire at which timing pulse T1–T12 under which branch-register (BR) condition. `src/core/cpu/subinst.c` is a direct transcription of these tables. |
| `AGC4-Memo-9-Block-II-Instructions.pdf` | The scanned original (Blair-Smith, 1966) | The transcription covers pp. 30–50 only; the scan has the surrounding narrative and the opcode encoding. |
| `agc-information-series/agcis_32_blk2_instructions.pdf` (+ `_errata1`) | Information Series #32, Block II instructions | Independent statement of the instruction set; use to cross-check the memo. |
| `agc-information-series/agcis_2_machine_instructions.pdf` | Information Series #2, machine instructions | Earlier (Block I-era) but the clearest prose on instruction semantics. |

## Sequence generator, timer, and priority control

| File | What it is |
|---|---|
| `agc-information-series/agcis_7_sequence_generator.pdf` | The sequence generator subsystem — how SQ/ST/BR select a subinstruction and how the cross-point generator turns that into control pulses. |
| `agc-information-series/agcis_5_timer_sequence_generator.pdf` | The timer: master oscillator, clock division, the scaler stages, and the pulse trains derived from them. |
| `agc-information-series/agcis_8_priority_control.pdf` | Counter cells, involuntary sequences (PINC/MINC/PCDU/MCDU/DINC/SHINC/SHANC), interrupt priority, and how counter requests steal MCTs from the program. |
| `agc-information-series/agcis_16_fresh_start.pdf` | GOJAM / fresh start / restart behaviour. |
| `agc-information-series/agcis_17_keyrupt_partial.pdf` (+ `_errata`) | KEYRUPT, UPRUPT, MARK and the DSKY interface. |

## Memory

| File | What it is |
|---|---|
| `agc-information-series/agcis_4_erasable.pdf` | Erasable (coincident-current core) memory, including the destructive-read/rewrite cycle that fixes the T4 read and T10 writeback windows. |
| `agc-information-series/agcis_10_fixed.pdf` | Fixed (core rope) memory, bank/superbank addressing, and parity. |
| `agc-information-series/agcis_3_central_processor.pdf` | Central registers A, L, Q, Z, B/C, G, S, X/Y/U adder. |

## Whole-machine and gate level

| File | What it is |
|---|---|
| `agc-information-series/agcis_30_block_ii_agc.pdf` (+ `appendix_a`, `appendix_b`, `errata`) | Information Series #30: the Block II AGC as a system. |
| `AGC4-Logical-Description-1963.pdf` | Hopkins/Alonso/Blair-Smith, "Logical Description for the Apollo Guidance Computer (AGC4)" — the design-era logic description. |
| `R-700.pdf` | Eldon C. Hall, *MIT's Role in Project Apollo*, vol. III: **Computer Subsystem** (1972) — the final report on the whole machine. Its §5 description of the sequence generator as "a wired memory ... the output ... formed by a cross-point generator as a logic product of the appropriate time pulses and instruction codes" is what licenses reading the cross-point netlists as a static logic product (`tools/oracle/gate_crosspoint.py`). |
| `the-apollo-guidance-computer_jp2/` | Alonso & Hopkins, "The Apollo Guidance Computer" (R-393, 1963), 40 page scans. Not committed; see above. |
| `E-2052-AGC4-Basic-Training-Manual.pdf` | MIT's own training manual, Volume I (1967). The gentlest introduction to the register/pulse model. |
| `block2-schematics/` | The original module-level NOR-gate schematics, mirrored from klabs.org. `index.html` is the module index; the sheets are in `logic/`. Modules that matter most: SCALER, TIMER, CROSS-POINT GENERATOR I/II (the control-pulse ROM), STAGE/BRANCH DECODING, COUNTER CELL I/II, PARITY AND S REGISTER, ALARMS, MEMORY TIMING & ADDRESSING, RUPT SERVICE. **These decide** when the memo and `ext/agcplusplus` disagree: `a04-2`/`a06-2` settled FINDINGS #9 and #11, and `a07-2` (SERVICE GATES, drawing 2005252) is where the MP3A carry term is *absent* — the one place the drawings and Mike Stewart's replica differ. |
| `agc-information-series/agcis_21_system_test.pdf` | The factory test procedures — a source of self-measuring test ideas. |

## Also vendored as submodules (not copied here)

- `ext/agcplusplus` — control-pulse-level emulator, MIT licence. The corrected
  reading of the memo's tables.
- `ext/agc_simulation` — Mike Stewart's gate-level Block II simulation.
- `ext/virtualagc` — yaAGC (instruction-level), yaYUL (assembler), the original
  MIT/Draper rope listings, and the physical rope-module dump library.

See `TEST_SOFTWARE.md` for the software shelf.
