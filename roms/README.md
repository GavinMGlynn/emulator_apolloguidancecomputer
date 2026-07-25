# roms/ — core ropes

Flight and test software, assembled from the original MIT/Draper listings in
`ext/virtualagc` by `tools/build_ropes.sh`. Each rope gets a directory holding:

- `<Rope>.bin` — 36 864 big-endian 16-bit words in the physical rope layout
  (`yaYUL --hardware`: data bits 1–14 in positions 1–14, parity in position 15,
  data bit 15 in position 16). This is what `--rope` loads.
- `<Rope>.symtab` — the symbol table, for finding the address of a cell to dump.
- `<Rope>.lst` — the full assembly listing.

Nothing here is committed: the images are large and are exactly reproducible
from the submodule. Run `./tools/build_ropes.sh` to repopulate.

`docs/references/TEST_SOFTWARE.md` says what each rope stresses and why it is on
the shelf. The short version: **Validation** is MIT's own instruction validation
suite and is the most valuable rope here; **Luminary099** and **Comanche055**
are the Apollo 11 LM and CM.

The original Apollo flight software is in the public domain.
