# bios/ — rope-module dumps

**The AGC has no BIOS.** There is no boot ROM, no firmware layer, and nothing
between power-on and the flight software: GOJAM sets the program counter to
04000 and the core rope takes over on the very next Memory Cycle Time. On this
machine the rope *is* the firmware.

So this directory holds the closest analogue: **dumps read out of real,
physical core-rope modules**, staged from
`ext/virtualagc/Rope-Module Dump Library` by `tools/build_ropes.sh` into
`bios/rope-modules/`. They are kept apart from the assembled ropes in `roms/`
for a reason — several carry genuine manufacturing and ageing defects (bad
strands, broken cores, flipped bits), and booting them is how we prove the
fixed-memory **PARITY FAIL** alarm behaves like the hardware's instead of being
silently swallowed. The `-Repaired` variants are the control.

A dump is a bank pair or a module's worth of words, not a whole 36 K rope, so it
loads at a bank offset:

```
agc_headless --rope-at 4:bios/rope-modules/2003053-041-BLK2-Retread50-B2.bin ...
```

Nothing here is committed. Re-run `./tools/build_ropes.sh` to repopulate.
See `docs/references/TEST_SOFTWARE.md` for what each module contains.
