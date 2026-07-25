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
| 10 | DV3/DV6/DV7 T5, T8, T11 | `DVST` present | `DVST` absent on those rows | AGCPlusPlus. DVST both advances the grey-coded stage and licenses the T3 restage; asserting it on every quarter would over-advance the stage counter. **Open** — worth a gate-level check. |
| 11 | MP3 T6 / T12 | `NEACOF` at T6, no T12 row | model differs | AGCPlusPlus. **Open.** |
| 12 | DV0 T1 | no `TMZ` | `TMZ` present | AGCPlusPlus. |
| 13 | DV4 T3 | `RU WB STAGE` | absent from the model | AGCPlusPlus omits it; DV4 is entered mid-MCT so a T3 row would belong to the previous stage. **Open.** |

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
