# Test software shelf — organised by the subsystem each rope stresses

The AGC has no "games"; its shelf is the flown mission ropes plus MIT's own
validation and development ropes. All of these are assembled from the original
listings in `ext/virtualagc` by `tools/build_ropes.sh`, which writes them to
`roms/<Rope>/<Rope>.bin` (yaAGC binary format: 36 864 big-endian 16-bit words,
each `value << 1 | parity`) alongside a `.symtab` and the full `.lst` listing.
The listing is how you find the address of a cell you want to dump.

Discipline (guide §11): these are **integration checks, never goals**. Complete
a subsystem with its unit tests first; boot a rope to find what the unit tests
could not.

## Instruction set and CPU

| Rope | Stresses | Notes |
|---|---|---|
| **Validation** | Every instruction, individually | Ed Smally's AGC instruction validation suite — our amidog-tests analogue, and the single most valuable rope here. One `Validate<OP>.agc` module per opcode plus `Smally*` checks for CCS, BZF, BZMF, DV, MP, MSU, counters, overflow, cycle/shift, and interrupts. It reports failures through `Errordsp.agc` rather than hanging, so a headless run can read the verdict out of erasable memory. |
| **Retread50** | Early Block II core | Small and boots fast; the shortest path from GOJAM to executing program code. Assembled `--no-checksums`. |
| **Aurora12** | Block II development rope with a self-check | Exercises the machine broadly without mission software's IMU/radar dependencies. |
| **SundialE** | Erasable and fixed memory addressing | Bank/superbank switching. |

## Whole-system / mission software

| Rope | Stresses | Notes |
|---|---|---|
| **Luminary099** | Everything | Apollo 11 LM ("the Eagle rope"). The famous 1201/1202 alarms are a *correct* emulation outcome under counter overload — a genuine exercise of the counter-steals-an-MCT path. |
| **Comanche055** | Everything | Apollo 11 CM. |
| **Luminary131** | Everything | Apollo 13/14 LM. Pairs with the physical module dump in `bios/rope-modules/Luminary131PlusLM131R1ModuleDump.bin` — assembling the listing and comparing against the hardware dump is a free end-to-end check of the rope loader. |
| **Artemis072** | Everything | Apollo 15–17 CM. |
| **Zerlina56** | Late experimental LM rope | Unflown; useful because it is unusual. |

## Physical rope-module dumps (`bios/rope-modules/`)

Dumps read out of *real* core-rope modules, staged from
`ext/virtualagc/Rope-Module Dump Library`. The AGC has no BIOS in the PC sense —
the rope **is** the firmware — so these are the closest analogue, and they are
kept apart from the assembled ropes because several carry genuine manufacturing
and ageing defects (bad strands, broken cores, flipped bits). Booting the
`-BadBits` / `-SomeDefects` variants is how we prove the **fixed-memory parity
alarm** fires like the hardware's rather than being silently swallowed; the
`-Repaired` variants are the control.

Block II modules staged: Retread50 B1/B2, Aurora85 B1/B2, Aurora88 B1/B2/B3,
SundialB B1/B2, SundialE B2/B3, and the Luminary 131 module set.

## Formats we accept, and the ones we don't

- **yaAGC `.bin`** — 36 864 words, big-endian, `value << 1 | parity`. The native
  format of `roms/`, produced by yaYUL with `--parity`. This is what the loader
  reads.
- **yaYUL `--hardware` layout** — same file shape but parity in bit 15 instead
  of bit 0. Not read; `tools/build_ropes.sh` never produces it. Documented here
  so a mystery parity storm has an obvious first suspect.
- **Rope-module dumps** — a bank pair (or module's worth) rather than a whole
  36 K rope, so they load at a bank offset, not at zero.
