# Contributing

The short version of how this project works. `CLAUDE.md` is the full set of
working conventions and `docs/USING_THE_AGC.md` is the operating manual; this
file is what you need before your first change.

## Get it running

```sh
git submodule update --init ext/unity          # all CI needs
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --preset linux-debug                     # 21 suites; must be green
```

For the flight software and the full set of oracles:

```sh
git submodule update --init ext/virtualagc ext/agcplusplus ext/agc_simulation
./tools/build_ropes.sh
```

Every test that needs a submodule skips cleanly when it is absent, which is why
CI stays green with only Unity. If you initialise the oracles you get five more
tests and they are the ones that matter most.

## The one rule that matters

**Resolve doubt from a source, never by adjusting a parameter until a test
passes.** The sources, in the order they win:

1. **AGC4 Memo #9** — `docs/references/AgcPulsesAndSequences.txt`, Hugh
   Blair-Smith's own transcription. Which control pulses fire at which timing
   pulse, under which branch condition. `src/core/cpu/subinst_tables.c` is
   generated so it stays diffable against this by construction.
2. **`ext/agcplusplus`** — a runnable model at the same abstraction. The memo
   has typos and omits pulses the hardware asserts implicitly; this is where
   those are resolved. Read it, do not link it.
3. **The gates** — `ext/agc_simulation` and the NOR-gate schematics in
   `docs/references/block2-schematics/`. When the memo and the model disagree,
   these decide. `tools/oracle/gate_crosspoint.py` reads a timeline straight out
   of the netlist and `gate_diff.py` sweeps all 1672 rows against our tables.

`ext/virtualagc`'s yaAGC is the fourth reference and a special case: it is
instruction-level, so it is good for architectural state and for assembling
ropes with yaYUL, and **never** a source of a cycle count.

If you find a place where the sources disagree, that is not an obstacle — it is
the interesting part. Write it up in `tools/oracle/FINDINGS.md` with what you
read and where. Several entries there are places this emulator is more faithful
than the model it was transcribed from, and each one started as a discrepancy
somebody characterised instead of smoothing over.

## What a change looks like

- **One item, landing with its test.** A red tree is the stop-everything
  condition.
- **Tests are hardware facts, named as sentences.**
  `test_ccs_leaves_the_diminished_absolute_value_in_a`, not `test_ccs_2`.
- **No instruction-level shortcuts in `src/core`.** One `agc_tick()` is one
  timing pulse. Counter interleaving, divide staging and the memory windows are
  emergent, and they stay that way.
- **Both living docs move in the same commit.** `docs/PROJECT_STATUS.md` (what
  now works and how it was verified) and `docs/COMPLETION_PLAN.md` (tick the
  item, and add any tails you found on the way).
- **Temporary instrumentation is reverted before you commit**, ours and
  anything under `ext/`.
- **Optimisation only under the identity harness.** Probe goldens plus a
  long-run state hash, byte-identical or it does not ship.

## Why the goldens must be bit-identical

Everything this project measures is an emulated timing-pulse count, not a
measurement of your computer, so the same arguments must produce the same bytes
on every platform and both build types. CI asserts that across Linux (clang and
gcc), RHEL, macOS and Windows.

A platform or build type where a golden differs has a real bug, and this is not
theoretical: an uninitialised struct field once routed an arbitrary subset of
`--press` keystrokes through the ground uplink instead of the keyboard, and
which subset depended on the optimiser. It survived a long time because not one
probe presses a key. See `tools/oracle/FINDINGS.md` #81 — including the part
about how the harness was sound and its coverage was not.

## Where to start

The tails at the end of `docs/COMPLETION_PLAN.md` are genuine open work, small
enough to finish, and each one already names the verification it needs. As of
now:

- **V37 is entered but never dispatched.** The flight software takes the
  keystrokes and displays them, but `V37XEQ` is never reached and the PROG field
  stays blank. Needs Luminary's listing read against a trace. The most
  interesting thing on the list.
- **Nothing presses a key in the identity harness except one test.** A keyed
  probe with a committed golden would close the hole described above properly.

Both are self-contained and neither needs the whole machine in your head.

## Reporting something

An issue that names the rope, the exact command line and what you expected is
worth ten that say the display is blank — and if the display is blank on a
flight rope, that is the rope waiting for you to key `V36E` at it, which
`docs/USING_THE_AGC.md` §5 explains.
