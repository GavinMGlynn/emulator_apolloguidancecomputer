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
| 9 | DAS1 T10/T11 | `10. x0 WL` / `10. x1 RU WA` | model splits differently across T10/T11 | AGCPlusPlus. Not yet independently confirmed against the gate level. **Open.** |
| 10 | DV3/DV6/DV7 T5, T8, T11 | `DVST` present | `DVST` absent on those rows | AGCPlusPlus. **Resolved.** With one DVST per sequence the grey counter runs 0→1→3→7→6→4, exactly the documented stage order; four per sequence would over-advance it. The memo's extra DVSTs must serve its *other* documented function — permitting the T3 restage — which is not counter-driven. Divide now matches the oracle on 13/13 cases. |
| 11 | MP3 T6 / T12 | `NEACOF` at T6, no T12 row | model differs | AGCPlusPlus. **Open.** |
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
