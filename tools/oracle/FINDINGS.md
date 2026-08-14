# Oracle findings

One row per question resolved against a reference. Nothing here is "fixed" on
reasoning alone; each row names the source that settled it.

The references, in the order we trust them for control-pulse questions:

1. **AGC4 Memo #9** — `docs/references/AgcPulsesAndSequences.txt` (Hugh
   Blair-Smith's transcription of pp. 30–50) and the scan alongside it.
2. **AGCPlusPlus** — `ext/agcplusplus`, a control-pulse-level model built with
   Mike Stewart's help. The corrected reading of the memo.
3. **Gate level** — `ext/agc_simulation` and
   `docs/references/block2-schematics/`. Decides ties.

`tools/gen_subinst_tables.py` re-derives our pulse tables from (2) and prints
every place it disagrees with (1). Everything that report emits is expected to
appear below; an unexplained line in that output is an open question.

## Memo transcription errors (memo wrong, model right)

| # | Where | Memo says | Reality | Source |
|---|---|---|---|---|
| 1 | TC0 T4 | `RU WZ` at T4 | `RU WZ` at **T6** | AGCPlusPlus. T4 would land the incremented Z before the T5 memory read has resolved, and TCF0 — which is otherwise identical — already prints T6. A typo in the memo. |
| 2 | GOJ1 T8 | `RSTSR` | `RSTRT` | No pulse named RSTSR exists in the memo's own definition list. RSTRT ("place octal 004000, the Block II start address, on the write lines") is the only reading that starts the machine at 04000. |
| 3 | DCA0 / DCS0 T1 | `W12` | `WY12` | No pulse `W12` is defined. |
| 4 | WRITE0 T2 | `RA WB WG` | `RA WB` | The memo itself flags this: "[WG may be a typo!]". WG at T2 would clobber the memory buffer before the channel read at T4. |
| 5 | WOR0 T5 | `RA RU WA WCH` | `RB RU WA WCH` | AGCPlusPlus. WOR ORs the accumulator into the channel; at T5 the accumulator's value is in B, and RA would OR the *pre-read* A rather than the buffered copy. |
| 6 | MP0 T9 | `RB WY` / `RB WY CI` | adds `RC` on the negative branches | AGCPlusPlus. |
| 7 | NDX1 T10 | `RU WB` (first printing) / `RU WB EXT` (second) | `RU WB EXT` for the extracode form only | The memo prints NDX1 twice, once under NDX0 and once under NDXX0, with different T10 rows. The second is the extracode INDEX, which must set the EXTEND flip-flop. |
| 8 | ADS0 T7/T9 | rows annotated "[no function; similar to DAS1]" | kept | These rows genuinely do nothing useful in ADS; the hardware asserts them because ADS0 and DAS1 share cross-point wiring. We keep them so the tables stay diffable against the memo — and because "asserts a pulse with no effect" is still a fact about the machine. |
| 9 | DAS1 T10/T11 | `10. x0 WL` / `10. x1 RU WA` | `10. x0 WL` / **`11. x1 RU WA`** | **Resolved at the gate level — see #47.** The T11 term is `DAS1 . BR2` in cross-point drawing 2005263; the memo's second "10." is a typo for "11.". The model was right. |
| 10 | DV3/DV6/DV7 T5, T8, T11 | `DVST` present | `DVST` absent on those rows | AGCPlusPlus. **Resolved.** With one DVST per sequence the grey counter runs 0→1→3→7→6→4, exactly the documented stage order; four per sequence would over-advance it. The memo's extra DVSTs must serve its *other* documented function — permitting the T3 restage — which is not counter-driven. Divide now matches the oracle on 13/13 cases. |
| 11 | MP3 T6 / T12 | `NEACOF` at T6, no T12 row | **memo right; we now follow it** | **Resolved at the gate level — see #48-50.** The gates clear the NEAC latch at T6 (it *is* TL15) and hold the carry off for the rest of MP3 with a separate MP3A term on the carry gate, which no document mentions. AGCPlusPlus models the same effect by deferring NEACOF to T12; we model the term. One place we are more faithful than the oracle. |
| 12 | DV0 T1 | no `TMZ` | `TMZ` present | AGCPlusPlus. |
| 13 | DV4 T3 | `RU WB STAGE` | absent from the model | **Resolved.** It is the same pulse the memo already prints as the last row of DV6: the transition into DV4, listed at both ends. DV4 itself begins at T4, and by its own T8 `RSTSTG` has cleared the divide flag so the MCT ends at T12 — a DV4 T3 row could never execute. Divide matches the oracle on 13/13 cases. |

## Pulses the memo does not list at all

| # | Signal | Where | Why it must exist |
|---|---|---|---|
| 14 | `1xP10` | DV0 T1 | Clears G. Without it, RG and RSC collide on the write lines at T7 of DV1 and divide produces garbage. Named for its cross-point coordinate, not a memo mnemonic. |
| 15 | `8xP5` | DV1 T8 | Sets S bit 12, redirecting the erasable read during divide. |
| 16 | `CLRIIP` | RSM3 T8 | RESUME drops the interrupt-in-progress line. **Found the expensive way:** our generator originally parsed only function calls, so this bare state assignment was silently dropped. The machine then ran normally for ~0.3 s of emulated time and restarted with RUPT LOCK — because IIP stayed set, no further interrupt could ever be taken. The generator now hard-errors on any unmapped bare assignment rather than dropping it. |

## Pulse ordering within a timing pulse

The memo prints the pulses of a row in a conventional order (reads, writes,
tests). That is not always the order they must *execute* in, because some pulses
gate others within the same timing pulse:

| # | Row | Memo order | Execution order | Why |
|---|---|---|---|---|
| 17 | DV1/DV3/DV6/DV7 T1, T4, T7, T10 | `L2GD RB WYD A2X PIFL` | `L2GD RB PIFL WYD A2X` | PIFL decides whether WYD may rotate WL16 into Y1. Printed after WYD, it would arrive a pulse too late. |
| 18 | DV1/DV3/DV6/DV7 T2, T5, T8, T11 | `RG WL TSGU DVST CLXC` | `RG TSGU CLXC/RB1F WL DVST` | CLXC and RB1F both read the BR1 that TSGU has just set. |

## Rope image layout

| # | Question | Answer | Source |
|---|---|---|---|
| 19 | Which yaYUL output layout does a hardware-level model want? | `--hardware`: data bits 1–14 in positions 1–14, **parity in position 15**, data bit 15 in position 16. | Read directly from `ext/virtualagc/yaYUL/yaYUL.c`. Reassembling the word costs one mask and one shift, and the sign lands in bits 15 *and* 16 for free. yaAGC's own `--parity` layout (parity in bit 1, data shifted up into 2–16) is a software convenience and would decode every opcode one bit off. `tools/build_ropes.sh` strips `--parity` and forces `--hardware` so the intent is unambiguous. |
| 20 | Do rope words carry odd or even parity? | Odd, across all 16 stored bits. | Measured over all 3171 non-zero words of the assembled Validation rope. An all-zero word therefore fails parity, which is how unwoven rope and missing modules announce themselves. |

## Behaviour confirmed by running the flight software

| # | Observation | Reading |
|---|---|---|
| 21 | Eight of the nine assembled ropes (Luminary 099/131, Comanche 055, Artemis 072, Zerlina 56, Validation, Retread 50, Sundial E) run 200 000 MCTs — 2.34 emulated seconds — with no hardware alarm. | The sequence generator, memory cycle, banking, scaler and priority control are consistent enough to carry real flight software through startup and into its main loops. |
| 22 | Aurora 12 latches RUPT LOCK between 20 000 and 60 000 MCTs. | **Open.** Aurora 12 is a 1966 development rope and may legitimately idle without arming an interrupt source — but it may equally be starving on the serial-counter sequences we have not implemented. Not yet characterised; see the completion plan. |
| 23 | Luminary 099's first four subinstructions after GOJAM are `TC0`, `STD2` (fetching `TC 4` = INHINT, with the inhibit flip-flop coming up set), `CA 04054`, `XCH 6006`. | Matches `GOPROG` in the Luminary 099 listing. The pseudo-code recognition in RAD — which decides INHINT/RELINT/EXTEND from the *address* during the fetch — is behaving. |
| 24 | Debug (`-O0`) and release (`-O3 -flto`) builds produce byte-identical state dumps and full erasable dumps after 200 000 MCTs on all nine ropes. | Emulated results are timing-pulse counts, not measurements. This is the property that makes goldens portable; CI asserts it on four platforms. |

## Measured against the memo (the `timing` probe)

Twenty-six instructions bracketed between sentinel stores and checked against
AGC4 Memo #9's sequence tables, not merely against a golden. All twenty-six
agree. The instruction cost is the window minus the four MCTs of `CA` + `TS`
overhead; extracodes include the MCT of the `EXTEND` in front of them.

| # | Instructions | Measured | Reading |
|---|---|---|---|
| 25 | TC, TCF, INHINT, RELINT | **1 MCT** (60-pulse window) | These fetch for themselves: TC0/TCF0 assert NISQ and RAD in their own final subinstruction, so no STD2 follows. The pseudo-codes cost exactly the one extra STD2 that RAD's `RZ ST2` forces. |
| 26 | CA, CS, AD, MASK, TS, XCH, LXCH, INCR, ADS, INDEX | **2 MCT** (72) | One subinstruction plus the STD2 that fetches the next instruction. INDEX is 2 because NDX1 carries NISQ, so the indexed instruction's own fetch is absorbed. |
| 27 | DAS, DXCH, and (with EXTEND) SU, MSU, QXCH, AUG, DIM | **3 MCT** (84) | Two subinstructions plus STD2, or one plus STD2 plus the EXTEND. |
| 28 | CCS *plus its branch* | **3 MCT** (84) | CCS itself is 2; the window necessarily includes the transfer out of one of its four branch words, because all four must be real instructions. Named `CCS_PLUS_TCF` in the probe so the figure is not mistaken for CCS alone. |
| 29 | DCA, DCS, MP (each with EXTEND) | **4 MCT** (96) | DCA/DCS are 3 (two subinstructions + STD2); MP is 3 (MP0, MP1, MP3 — MP3 carries NISQ, so no STD2). |
| 30 | DV (with EXTEND) | **7 MCT — exactly 84 pulses** (132-pulse window) | **The one worth checking.** DVST licenses a divide sub-sequence to end at T3 instead of T12, so a divide is assembled from unequal segments and there was no reason to assume the total landed on a whole MCT boundary. It does: 72 pulses for the divide itself, the documented 6 MCTs, to the pulse. Measured before it was asserted. |

## Emergent behaviour (the `counters` probe)

| # | Observation | Reading |
|---|---|---|
| 31 | The same 256-iteration CCS countdown loop takes 18 648 pulses with TIME6 disarmed and 19 020 with it armed: **372 pulses, exactly 31 whole MCTs, stolen** by a peripheral the program never interacted with. | This is the Apollo 11 1201/1202 mechanism, and it is emergent — priority control steals the cycles, nothing models "counter overhead". The count is consistent with TIME6's 1.6 kHz rate over an 18.6 ms window (≈ 30 ticks). The exact figure has no closed form, because the loop's own lengthening changes how many ticks fall inside it, so the probe asserts the invariants (stealing is not free; a stolen cycle is a *whole* MCT, T1 to T12) and lets the golden pin the number. |

## Branch asymmetry (the `branches` probe)

| # | Case | Measured | Reading |
|---|---|---|---|
| 32 | BZF / BZMF **taken** | **1 MCT** | The taken branch asserts NISQ and RAD in its own final subinstruction, so it fetches the next instruction for itself and no STD2 follows. |
| 33 | BZF / BZMF **not taken** | **2 MCT** | Nothing fetched, so an STD2 follows. The only cost asymmetry in the Block II instruction set, and the kind of thing an instruction-level emulator can get wrong indefinitely without any test noticing. |
| 34 | BZF branches on **both** zeroes: +0 (000000) and -0 (177777) | both taken, 1 MCT | Ones' complement has two zeroes that are numerically equal but not bit-equal. This is what the dedicated TMZ pulse is *for*, and a probe that only tested +0 would pass against a broken TMZ. BZMF likewise takes both zeroes and any negative. |

Measured by aiming every branch at the word immediately after it, so taken and
not-taken share one measurement window and control flow is identical either
way — the two cases differ only in the MCT count.

## Interrupt discipline (the `interrupts` probe)

| # | Observation | Reading |
|---|---|---|
| 35 | With overflow held in A, a pending T6RUPT is refused for **the entire 100-increment run** and taken at increment 100, the instant a `TS` resolves the overflow. The control — the identical loop with the identical interrupt source and a clean accumulator — takes it at increment **27**. | The hardware will not break into a program holding overflow, because A is not saved by the interrupt sequence and an overflowed A loses its overflow the moment it passes through erasable memory. The control is what makes this evidence: 100 on its own is equally consistent with an interrupt that merely arrived late. |
| 36 | After the interrupt, the program goes on to complete all 110 of its increments. | RESUME reloaded Z from ZRUPT correctly. Had it not, the program would have resumed somewhere else and never reached its final count. |
| 37 | ZRUPT and BRUPT both read `024100` after the run, and the handler's copies agree. | The interrupt sequence saved Z and B where RSM3 expects to find them. |

## Harness lessons

| # | Lesson | Cost |
|---|---|---|
| 38 | **A probe must stop itself.** The AGC has no halt, so a finished probe parks in a branch-to-itself — which is a pure transfer-of-control loop, which trips TC TRAP after ~10 ms, which GOJAMs, which re-runs the entire probe on top of its own results. The interrupt probe first reported a counter 122 times too large. | `regress.py` now refuses any probe with neither sentinels nor a `stop_at`. |
| 39 | `--mct` **accumulates**. The frontend adds successive `--mct` values rather than replacing them, so a probe passing `flags --mct 4000` got 104 000. | `regress.py` rejects `--mct`, `--timepulses` and `--sentinel` in `flags` and provides `mct` and `stop_at` directives instead. |

## A real defect: divide lost its most significant quotient bit

| # | Finding |
|---|---|
| 40 | **Divide returned the wrong answer in every case**, and the error had a shape: the top quotient bit was missing. 35÷5 gave 3 instead of 7; 6144 came back as 2048; 1792 as 768. Ours disagreed with the oracle on 13 of 13 test cases. |

The cause was in `tools/gen_subinst_tables.py`, not in the pulse tables or the
memo. The divide sequences are written like this:

```cpp
case 2:
    rg(cpu);
    tsgu(cpu);                    // <-- sets BR1 from the sign of the sum
    switch (cpu.br) {             // <-- tests the value TSGU just produced
    case 0b00: case 0b01: clxc(cpu); break;
    case 0b10: case 0b11: rb1f(cpu); break;
    }
    wl(cpu);
    dvst(cpu);
```

The generator hoisted that `switch (cpu.br)` into a **row condition**. But a row
condition is evaluated by the executor *before any pulse of the timing pulse
runs*, whereas the hardware evaluates this one *after* TSGU has written BR1.
So the row was chosen on the stale BR1: on the pulse where the divide should
have deposited a quotient bit via RB1F, the CLXC row was selected instead and
the bit was simply never written. One bit lost per divide.

The fix is in the generator. A branch switch that follows a BR-writing pulse
(`tsgn`, `tsgn2`, `tsgu`, `tmz`, `tov`, `tpzg`, `tl15`) in the same timing pulse
is now flattened to the **union** of its arms rather than hoisted. That is sound
here — and only here — because both pulses involved, CLXC and RB1F, re-test BR1
themselves; the generator hard-errors if a late conditional ever selects a pulse
that does not. Divide now agrees with the oracle on all 13 cases, and the ropes
still boot clean.

Worth noting what did *not* find this. Every unit test passed. All 26
instruction timings matched the memo — the divide took exactly the right number
of timing pulses while computing the wrong number. Five flight ropes booted for
2.34 emulated seconds without an alarm. It took a probe that did arithmetic and
looked at the answer.

| # | Finding |
|---|---|
| 41 | **A straight run of 240 arithmetic instructions trips TC TRAP.** The first integrity probe unrolled its round trips into 240 consecutive MP/DV instructions with no transfer of control anywhere, and the machine restarted itself. That is the emulator being right: the alarm wants to see both a TC and a non-TC inside each ~5 ms window, and no real AGC program runs 240 instructions without a branch. The probe was rewritten as a loop. |

## The oracle harness

`tools/oracle/build_oracle.sh` builds a runnable oracle out of
`ext/agcplusplus`: load a rope, tick N times, dump erasable cells. Upstream's
own binary wants sockets, a DSKY, threads and a wall clock, none of which a
comparison needs, so this host links only the Block II core and drives
`cpu.tick()` directly. `ext/` is never modified — the two things this toolchain
needs (a prelude of headers, a stub `sockpp`) are supplied from outside with
`-include` and `-I`.

With `ORACLE_TRACE=1` it logs one line of state per timing pulse, which is what
makes a pulse-by-pulse diff against our own `--trace` possible. Finding #40 came
down to a single line of that diff: at DV6 T11 the oracle had `L=060001` and we
had `L=060000`.

Two things to know if you use it. It cannot tick the scaler — that drives the
CDU, which spawns a thread it never joins, which is harmless in a program that
runs forever and fatal in a short-lived host. And it is a *model*, not hardware:
where it and the memo disagree, the gate-level references decide.

## Differential testing the instruction set

`tools/oracle/differential.py` runs every instruction against the oracle over a
sweep of operand values chosen for the awkward cases — both zeroes, the largest
positive, the largest negative, alternating bit patterns — and compares the full
16-bit registers as well as the erasable cells touched.

| # | Result |
|---|---|
| 42 | **2496 of 2496 cases agree**, across CA, CS, AD, SU, MASK, TS, XCH, LXCH, QXCH, INCR, ADS, AUG, DIM, MSU, CCS, MP, DV, DAS, DXCH, DCA and DCS. |

It compares registers rather than only memory because bit 16 is the overflow
bit, and anything routed through erasable memory on its way out loses it: an
instruction leaving the wrong overflow state would look perfectly correct in a
memory dump. The one normalisation applied is in the other direction — the
oracle reads erasable through the destructive read, which duplicates the sign
into bit 16, while we dump the raw cell, so the comparison masks to the 15 bits
actually stored.

This is the check that should have existed before FINDINGS #40. A quick sweep
runs in CTest; the full one takes about two minutes and is run by hand.

| # | Harness lesson |
|---|---|
| 43 | **The differential rope must be per-process.** A background full sweep overlapping another invocation shared one `build/differential.bin` and reported 403 phantom mismatches — including a confident "DCS 112/256", which passed 256/256 the moment it was run on its own. Each run now writes its own PID-suffixed rope and removes it at exit. Worth remembering the shape of it: a differential test that shares mutable state with itself will accuse the thing it is testing. |

## The counter storm, and two ways of measuring it wrong

| # | Finding |
|---|---|
| 44 | **F05A was never implemented.** The scaler's own header comment listed the 3.2 kHz tap that turns channel-14 bits into drive-counter requests, and the code did not have it — so the six counters a program can raise against itself never requested anything. Found by a probe that set those bits and measured no slowdown at all. |
| 45 | **With it, a program can starve itself measurably.** The same 64-iteration loop takes 396 MCTs quiet, 404 with TIME6 counting, and **529 under six drive counters at 3.2 kHz** — a third of the machine gone to peripherals the program never reads. This is the Apollo 11 1201/1202 mechanism at full strength, and it is emergent: priority control steals the cycles, nothing models "counter overhead". |

The figure is arithmetically consistent, which matters because the first two
attempts were not:

- 6 counters x 3200 Hz over the 6.2 ms window predicts 119 stolen cycles;
  measured 125, the balance being TIME6 still running.
- Each drive counter shows exactly 20 decrements, and 6 x 20 = 120.

**The first measurement said 5597 stolen MCTs — eleven times the truth.** The
probe ran 256 iterations, which took it past ~120 ms, at which point RUPT LOCK
correctly restarted the machine: a program that never enables an interrupt
source is exactly what that alarm is for. The restart re-ran the whole probe,
so the closing sentinel fired on the second pass and the window it reported was
fiction. Nothing in the harness objected — the run had a `stop_at`, the goldens
were self-consistent, and the number was merely enormous.

What caught it was arithmetic. 5597 stolen cycles needs ~66 000 requests per
second and six counters at 3.2 kHz can only produce 19 200; and the counter
cells themselves had decremented only 77 times each, which is not 5597. The
lesson is narrower than "check your work": **an emergent measurement should be
reconciled against the rate that is supposed to produce it, and against a
second, independent count of the same events.** A probe that only reports a
duration cannot tell you it has been restarted.

The loop is now short enough to finish well inside the alarm window, rather than
suppressing the alarm — the machine is behaving correctly in both cases and only
one of them is a useful experiment.

## The gate level, and the third opinion on the pulse tables

Rows 9 and 11 above were the last two places where we followed AGCPlusPlus over
the memo with nothing but the model's word for it. Settling them needed the
gates, and the gates are not runnable here — `ext/agc_simulation` is Icarus
Verilog and the whole machine, which is far more than a cross-point question
needs. So the netlists are read directly instead, by
`tools/oracle/gate_sim.py`: three primitives (`nor_1..4`, `od_buf`, `pullup`)
transcribed from their own source files, the 74xx packs parsed for pin order,
and a synchronous unit-delay evaluation of the mesh. R-700 vol. III (Hall,
p. 5-6) is the licence for reading it that way — the sequence generator is "a
wired memory ... the output ... formed by a cross-point generator as a logic
product of the appropriate time pulses and instruction codes."

`tools/oracle/gate_crosspoint.py` holds modules A3/A4/A5/A6 at one
subinstruction and walks T1-T12, printing the same table the memo prints.
`tools/oracle/gate_diff.py` runs the sweep against our tables in CTest.

| # | Question | Answer | Source |
|---|---|---|---|
| 46 | Where does the machine take its subinstruction encoding from? | SQ register bits 16,14,13,12,11 plus SQR10 and the EXTEND flip-flop, decoded to one-hot SQ0..SQ7 lines inside A3. | Derived by sweeping those bits in the netlist and reading which decode line comes up; the result reproduces the memo's own "Op Code EXT SQ16,14-13,12-11,10" table exactly, which is the check that the driving is right rather than merely plausible. |
| 47 | **#9 closed: DAS1's second `RU WA` is at T11, not T10.** | The memo prints `10. x0 WL` and `10. x1 RU WA`; the gates put `WL` at T10 when BR2=0 and `RU WA` at **T11** when BR2=1. Our tables were already right. | The T11 term is `DAS1 . BR2` (A6 U6044 y1), reaching WA through U6042/U6041 and RU through U6036. Captured timeline below. The memo's second "10." is a typo for "11.", which the ADS0 sequence — sharing the same cross-point wiring, and printed at `11. xx` — already hinted at. |
| 48 | **#11 closed, and not the way either source said.** | NEACOF fires at **MP3 T6**, exactly where the memo prints it. It is not a cross-point output at all: it is `TL15`, the same pulse that copies L15 into BR1, clearing the NEAC latch as its second effect. | A4 gate 36449 gives `TL15 = MP3 . T06` (drawing 2005262); A6 gates 40426/40427 are the NEAC latch, set by MP0T10 and reset by TL15 or GOJAM (drawing 2005263, pin 458). |
| 49 | Then how does multiply survive, when its final sum is not formed until MP3 T11? | **A second, independent inhibit that no document mentions.** The service gates form the carry into bit 1 as `CINORM = NOR(NEAC, EAC, MP3A)`, and `MP3A` is the bare MP3 decode line — so the end-around carry is off for the whole of MP3 regardless of the latch. | `ext/agc_simulation` module A7 gate 33457 (three inputs). **The original drawing 2005252 has only two**: NEAC and EAC. The third input exists in Mike Stewart's hardware replica and not in the drawing revision mirrored at klabs, which is the "hardware fix" AGCPlusPlus's own comment refers to. |
| 50 | Does it matter? | Yes. With NEACOF at T6 and no MP3A term, `MP` is wrong for **8 of 100** operand pairs — every one whose high half lands on -0, which the live carry turns into +1. `037777 x 1` comes out 040000 too big. | Measured on our core with the row moved, against the arithmetic product. AGCPlusPlus reached the same place from the other end: commit 4cbe538, "Some small tweaks and fixes for MP / Still not passing that first set of tests" — MIT's own Validation rope. |

We now model it the way the gates do rather than the way the reference model
does: NEACOF stays at MP3 T6 where the memo and the cross-point drawing put it,
and `mp3a` inhibits the carry in `agc_cpu_update_adder`. That is one place we
are more faithful than the oracle, and it is a *deliberate* divergence, encoded
as `GATE_CORRECTIONS` in `tools/gen_subinst_tables.py` so the generator cannot
quietly undo it on the next submodule bump.

Two consequences worth stating, because neither was obvious:

- **MP3A follows the decode, not the injected sequence.** A counter request
  serviced in the MCT between the last MP1 and MP3 leaves SQ and the stage
  counter alone, so MP3A is still up and that counter's increment runs with the
  end-around carry inhibited. Emergent, faithful to the netlist, and not
  verified against anything else — flagged here rather than smoothed over.
- **Multiply had no unit test at all.** Three timing probes, a 2496-case
  differential sweep and nine booting ropes did not notice, because the
  reference model shared the same behaviour and the differential test can only
  find places where we disagree with it. `cpu_suite` now asserts the product
  arithmetically.

### The captured timelines

`tools/oracle/gate_crosspoint.py DAS1` — the T10/T11 question, settled:

```
  BR = 00                      BR = 01
    9.  RC  TMZ                  9.  RC  TMZ
   10.  WL                      11.  RU  WA
```

`tools/oracle/gate_crosspoint.py MP3` — the T6/T12 question, settled:

```
    5.  CI  RZ  WY12
    6.  RU  TL15/NEACOF  WZ        <- and nothing at T12
    7.  A2X  RB  WY                   (BR1 = 1)
   11.  RU  WA                        (BR1 = 1)
```

### What the sweep says about everything else

1392 rows — 29 subinstructions x 4 branch values x 12 timing pulses — agree with
the gate netlist with no exceptions left over. The two that were open when the
sweep was first run were exactly #11 above and RESUME's `CLRIIP`, which is not a
cross-point output at all (it lives in A15; FINDINGS #16).

| # | Harness lesson |
|---|---|
| 51 | **A subset of a netlist is not the netlist.** Open-drain nets are wired-ANDs whose pull-up sits on whichever module had room — `WA_n` is pulled up on A6, `Z16_n` over on A11. Loading four modules picks up drivers whose pull-up is out of scope, and such a net latches low the first time anything pulls it, which is how DAS1 first appeared to assert `Z15`, `Z16` and `L16` that no source prints. The simulator now supplies the missing pull-up explicitly. |
| 52 | **A zero-timing evaluation has no opinion about a latch.** NEAC, PIFL and GNHNC are SR latches; with neither term asserted they hold state in hardware and ring in the model. Rather than hide it, `settle()` returns the ringing set and `read()` refuses to report a value from it — so a latch can never be mistaken for a control pulse. Both memo pulses that *are* latches are reported by their set term instead. |
| 53 | **The idle divide counter leaks.** The grey counter in A4 free-runs with no divide in progress, and its arbitrary state reached the cross-point matrix as a phantom `WB` on DAS1 T3. The bench holds the divide conditions quiet for the non-divide sequences it probes; probing the DV sequences needs that counter driven stage by stage, which it does not attempt. |

## The DSKY

Five separate encodings, none of them in one place. Recorded here with the
source that settled each, because the temptation with a display is to copy a
table from another emulator and never learn where it came from.

| # | Question | Answer | Source |
|---|---|---|---|
| 54 | What is in a channel 10 relay word? | Bits 15-12 a bank number (octal 00-14), bits 11-1 the eleven relays of that bank. Banks 00-10 drive the three five-digit registers, 11-13 the NOUN, VERB and PROGRAM displays, 14 the status lights. | **Information Series #30**, table 30-5 and paragraphs 30-77 / 30-145A-C, read off the rendered page rather than an OCR extraction. |
| 55 | Which relay code is which digit? | `0->025 1->003 2->031 3->033 4->017 5->036 6->034 7->023 8->035 9->037`, blank 000. | **MIT's own flight software**: `RELTAB` in Luminary 099's fixed-fixed constant pool is exactly this table. Confirmed independently against ext/virtualagc's seven-segment artwork, whose files are named by relay code — the two agree on all eleven. |
| 56 | Which five-bit code is which key? | `1-7` are themselves, `010`/`011` are 8 and 9, `020` is 0, `021` VERB, `022` RSET, `031` KEY REL, `032` +, `033` -, `034` ENTR, `036` CLR, `037` NOUN. | **MIT again**: the `CHARIN` dispatch table in PINBALL, where every entry carries a comment naming its key, and everything unlisted falls through to `CHARALRM`. |
| 57 | Where does the flash come from? | Not from software at all. Module A24 gate U24025 forms `FLASH = NOR(FS17, FS16)` straight off the scaler; module A16 gate U16047 gates the VERB/NOUN displays with it and the KEY REL and OPR ERR lamps with its *complement*. | The gates. Our scaler already computed the same term, which is a pleasant confirmation from the other direction. |
| 58 | The bank field is four bits but only thirteen banks exist. | Codes 15-17 address no relays and do nothing. | Found by booting the Validation rope, which writes them: our first version indexed a 13-entry array with a 4-bit field and corrupted the heap on the first run. The rope regression caught it within a minute of the module existing. |

### Reading a rope's display back

The check the plan asked for, and it closed better than expected. **Sundial E**
puts up `VERB 05 NOUN 31` with `01107` in R2. Its own listing says why:
`ALARM_AND_ABORT.agc` displays `FAILDISP OCT 00531` — which reads V05 N31 — and
`FRESH_START_AND_RESTART.agc` sets alarm code `1107` with the comment "SET
ADDITIONAL FAILURE TO SHOW PHASE TABLE DISAGREEMENT (WILL BE DISPLAYED **IN
R2**)". Bank assignment, digit codes and register placement all confirmed at
once, against the source of the program doing the displaying.

(The alarm itself is correct behaviour: a rope started cold has no valid phase
table, and saying so is what that code is for.)

| # | Finding |
|---|---|
| 59 | **The flight ropes display nothing until they are talked to.** Luminary 099, Comanche 055, Artemis 072 and Zerlina 56 all run DSPOUT — 208 relay words in 23 emulated seconds, cycling every bank — and every one of them is blank, with the PROG light on. A keypress does reach the software: pressing VERB lights KEY REL, which is `CHARIN`'s documented response to a key arriving while a flashing display is pending. Whether these ropes should reach a V37 display from a cold start without further input is a question about the ropes, not about the DSKY, and is a plan item rather than a fix. |
| 60 | **A restart leaves the display standing.** GOJAM clears the output channels, so channel 11's lamps go out, but channel 10 only ever *addresses* a relay bank — clearing it selects bank 0 and resets nothing. The latching relays keep showing whatever they held. That asymmetry is free if the DSKY watches the same channel writes the machine makes, and wrong in both directions if it is modelled as a decoded display that gets cleared. |

## The serial counters, and a bug they exposed

| # | Question | Answer | Source |
|---|---|---|---|
| 61 | What do SHINC and SHANC do? | A fifteen-bit left shift of the addressed counter, bringing in a zero (SHINC) or a one (SHANC). The whole difference between the two sequences is a `CI` at T5. | AGC4 Memo #9. **`ext/agcplusplus` has neither** — the serial counters are the one part of priority control the reference model left out — so here the memo is not merely the primary source but the only one. The generator now emits them from its own parse of the memo (`FROM_THE_MEMO`), so they stay diffable rather than hand-copied. |
| 62 | Which request line runs which sequence? | `INLNKP` -> SHINC, `INLNKM` -> SHANC; likewise RNRADP/M for the radar and OTLNKM for the downlink. | Information Series #30 **table 30-7**, read off the rendered page. Confirmed against the gates: module A19 turns UPL1 into INLNKP and UPL0 into INLNKM. |
| 63 | What raises UPRUPT? | Not a counter and not a timer. Every uplink word starts with a flag bit, always a one; it walks up the register as the data arrives behind it and, when it is shifted out through bit 16 of the adder, TSGN has it in BR1 and WOVR turns that into the interrupt. | Information Series #30 paragraph 30-119, whose wording — "position 16 of the **Adder**" — is the detail that makes it work, since the flag never lands in the counter at all. |
| 64 | How fast can the ground send? | One bit per 156.25 microseconds, the period of scaler stage 4. A bit arriving inside that window is dropped and channel 33 bit 11 is set to say so. | Information Series #30 paragraph 30-119A. |

### The bug: a sign correction in the wrong place

Implementing the shift turned up a defect that had nothing to do with the
uplink. `agc_memory_write_erasable` was collapsing bit 16 into bit 15 on every
erasable write — "the duplicated sign collapses back". That is *right* for
almost everything, and it is what makes a counter wrap: TIME1 at 037777
incremented is 040000, positive overflow, whose corrected sign is +, so +0 lands
in core and WOVR carries the 1 into TIME2. Take it away and the AGC's 28-bit
clock stops being a clock. `ext/agcplusplus` does exactly the same thing, in the
same place.

But a shift-in moves its outgoing bit through bit 16 *while bit 15 still carries
data*, so correcting the sign there overwrites a real bit with the bit on its
way out — and every uplink word silently loses one. Which is why the first
working shift assembled the right answer and never interrupted.

The hardware makes the distinction, and makes it in the write path rather than
in the core: module A7 gate U7006 gates the normal G write with **SHIFT** (and
with NEAC, multiply's equivalent of the same problem). So the correction moved
out of `memory.c` — which now simply stores fifteen bits, because that is all a
core stack has — and into the rewrite, where it can ask what the machine is
doing. Two tests moved with it, from asserting the rule at the memory helper to
asserting it through a real store.

| # | Finding |
|---|---|
| 65 | **A latent bug can be invisible because two wrongs agree.** The old rule was wrong for shifts, but nothing shifted, so nothing noticed — and the differential test could not notice either, because the oracle collapses the same bit in the same place. It took implementing a subsystem the reference model does not have to expose it. That is worth remembering about differential testing generally: it finds where you disagree with the model, never where you agree with it and both are wrong. |
| 66 | **Luminary 099 acts on an uplinked word.** Four triple-redundant key codes sent from the ground raise UPRUPT and the rope's own handler lights UPLINK ACTY — `CAF BIT3 / WOR DSALMOUT`, "TURN ON UPACT LIGHT", the first thing UPRUPT does. Sixty-four bits sent, sixty-four accepted, none refused. What this does *not* yet show is the rope acting on the *content*: the lamp is lit before the redundancy check runs, and a deliberately corrupted word produces the same lamps, because these ropes are in the same waiting state that leaves their display blank. |

## The CDU

| # | Question | Answer | Source |
|---|---|---|---|
| 67 | How does the AGC learn an angle? | It does not read one. Each converter sends a pulse per CDU count of movement — PCDU one way, MCDU the other — and priority control steps an erasable counter. The computer keeps a running total the hardware nudges, so a missed pulse is simply a wrong angle until something zeroes it. | Information Series #30 paragraphs 30-90 onward, table 30-7. |
| 68 | And how does it drive one? | Through different counters entirely. The program loads a drive counter, enables that axis in channel 14, and the scaler asks for a DINC at 3.2 kHz until the count runs out; each DINC emits POUT or MOUT, and ZOUT takes the axis's own bit back out of channel 14. The rate is the hardware's — a large angle just takes longer. | Information Series #30 paragraph 30-91 and the DINC rows of the memo. |
| 69 | What does "zero IMU CDU's" zero? | **The converter, not the counter.** Channel 12 bit 5 stops the CDU tracking and sending; the running total in erasable is the program's own and the program clears it. | Luminary 099 settles it outright: `ZEROICDU  CAF ZERO ... TS CDUZ`, commented "ZERO ICDU COUNTERS", called right after raising the discrete. A first version of our CDU cleared those counters in hardware and would have hidden a program that forgot to. |
| 70 | Where does a DINC counter come to rest? | On **minus** zero. MONEX puts -1 in X, so the last step down from +1 is 1 + (-1), which in ones' complement is -0; the next DINC finds it with TMZ and stops. | Measured, and consistent with the memo's DINC rows: the `x1` branch that runs ZOUT is the one TMZ selects. |

| # | A real defect: ZOUT could not stop the X drive |
|---|---|
| 71 | `ZOUT` cleared the axis's channel 14 bit by reading the channel, masking, and writing it back through `agc_cpu_read_channel`/`agc_cpu_write_channel`. Those two wrap the fixup a *program* sees — a channel carries write-line bits 1-14 and 16, with no bit 15 of its own — so the read sign-extends bit 15 into bit 16 and the write puts it straight back. The X axis is bit 15. Its drive could therefore never be turned off, and would have run for ever once started; Y and Z, at bits 14 and 13, were fine. ZOUT is hardware clearing a flip-flop, not a program storing a word, and now goes at the channel directly. Found by the test that asserts the drive stops. |

## Aurora 12's RUPT LOCK, characterised

Open since the first rope boots, and resolved without changing a line of the
core: it is our own frozen input discretes, and the rope is behaving exactly as
written.

What happens, in order:

1. Aurora's T4RUPT handler compares channel 32's low eight bits against
   `LASTFAIL`. At power-up `LASTFAIL` is +0 and our channel 32 reads all ones —
   channels 30-33 are inverted, and we freeze them at their idle state because
   there is no spacecraft — so the comparison says the RCS status has *changed*
   and it calls `RCSMNTR`.
2. `RCSMNTR` reads channel 32 and complements it: `COM  # FAILURES NOW ONES`.
   The channel is inverted, so a one means healthy, and complementing all ones
   gives **+0** — "nothing has failed at all".
3. It then hunts for the highest set bit with a loop that exits on the overflow
   skip of a `TS`:

       25,3434  NXTRCSPR  INCR   FAILCTR
       25,3435            DOUBLE
       25,3436            TS     FAILTEMP   # OVERFLOW CHECK
       25,3437            TCF    NXTRCSPR

   Doubling +0 gives +0 for ever, `TS` never overflows, and the loop never
   ends. `FAILCTR` counts up until the interrupt has been in progress for long
   enough that RUPT LOCK restarts the machine — at MCT 32 427, every time.

| # | Finding |
|---|---|
| 72 | **Aurora 12's RUPT LOCK is ours, and it is one input bit wide.** Hold any one of channel 32's low eight bits low — report a single RCS failure — and Aurora runs 200 000 MCTs with no alarm at all. Leave the channel at its idle all-ones and it restarts at MCT 32 427 without fail. The rope's bit-search loop simply has no exit for "no failures", which for a 1966 development build is a plausible thing to have left out, and which it can only reach because a cold `LASTFAIL` of +0 makes "no failures" look like a change of status. |
| 73 | **It is not an arithmetic defect, and that can be said with evidence rather than by inspection.** The loop turns on `DOUBLE`, on `TS`'s overflow skip and on `MP` by +0, and MIT's own Validation suite exercises all three and passes. The chain was still checked directly: A is +0 at the top of every pass, `FAILCTR` climbs, `FAILTEMP` stays +0. |

The deliberate approximation that causes it stays as it is. Freezing the
spacecraft discretes at their idle state is the honest default for a machine
with no spacecraft attached, and inventing a failure to make one rope happier
would be the wrong trade — but a frontend that wants Aurora 12 to run past this
point now knows exactly which bit to hold.
