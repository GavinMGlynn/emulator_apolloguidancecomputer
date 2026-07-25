# Probes

Bare-metal AGC programs that make the machine demonstrate a property of itself,
run under the headless frontend, and locked into CTest as no-reference goldens.

## Why the harness does the timing

The guide's pattern is to have the machine measure *itself*: read a hardware
counter, do the thing, store the delta. The AGC cannot do that. Its finest
software-readable clock is TIME6 at 1.6 kHz — one tick per 53 Memory Cycle
Times — and TIME1 is coarser still at 100 Hz. There is no cycle counter to read.
Amplifying by looping does not rescue it either: a 10 000-iteration loop timed
against TIME1 gives a few percent, and a few percent is not cycle accuracy.

So the split here is:

- **The probe measures values.** It exercises a behaviour and leaves results in
  erasable memory, exactly as a self-measuring probe would.
- **The harness measures time.** A probe signals a moment by storing a non-zero
  word in a watched cell; `agc_headless --sentinel` records the emulated
  timing-pulse count at which that happened.

Nothing is lost by this. The recorded number is an *emulated* timing-pulse
count, not a measurement of the host, so it is exact, deterministic, and
identical on every platform and build type — the same property that makes the
goldens portable. What would be lost is the ability to run the same probe on
real hardware, and there is no real Block II AGC to run it on.

## What each probe checks

| Probe | What it establishes |
|---|---|
| `timing` | The MCT cost of 26 instructions, each bracketed between two sentinel stores. **Asserted against AGC4 Memo #9**, not just against a golden: an instruction whose subinstruction chain is one MCT wrong fails here even if it computes the right answer. |
| `counters` | That a counter request steals a whole MCT from a program that never asked for it — the mechanism behind the Apollo 11 1201/1202 alarms. Measures the same loop twice, once with TIME6 disarmed and once armed. |
| `branches` | The only cost asymmetry in the instruction set: a taken BZF/BZMF fetches for itself and costs 1 MCT, one not taken buys an STD2 and costs 2. Also that *both* zeroes branch — ones' complement has +0 and -0, and a probe that only tested +0 would pass on a broken TMZ pulse. |
| `interrupts` | That an interrupt is refused while the accumulator holds overflow, and that RESUME puts the program back exactly. A *value* probe: it asserts on what the machine computed, not on how long it took. |

## Controls

A probe that only shows the expected outcome usually has not shown anything.
`interrupts` runs its experiment twice — once with overflow held in the
accumulator, once with it clean — because a phase-1 result of "the handler ran
at increment 100" is equally consistent with an interrupt that was refused for
90 increments and one that simply arrived late. The control settles it: same
loop, same interrupt source, clean accumulator, handler at increment 27.

When a probe's claim is causal, give it a control.

## The measurement window

Each measurement brackets its instruction:

```
        <setup>          ; outside the window
        CA  ONE
        TS  Sa           ; window opens
        <instruction under test>
        CA  ONE
        TS  Sb           ; window closes
```

Both ends are stored by identical `TS` instructions, so the partial MCTs at
either end cancel exactly and the window is `cost + 4` MCTs — one CA and one TS
of overhead. Setup goes before the window so it cannot contaminate it.

Two consequences worth knowing:

- **CCS is measured with its branch.** CCS skips into one of four words, all of
  which must be real instructions that rejoin, so the window necessarily covers
  the transfer too. The measurement is named `CCS_PLUS_TCF` for that reason.
- **Extracodes carry their EXTEND.** EXTEND is a pseudo-code costing an MCT of
  its own, and it cannot be measured alone — the `CA ONE` that closes the window
  would be swallowed as the extracode.

## Running them

```
python3 tools/probes/gen_timing_probe.py     # regenerate tests/probes/timing.*
python3 tools/probes/gen_counter_probe.py    # regenerate tests/probes/counters.*
python3 tools/regress.py --exe build/linux-release/src/frontend/headless/agc_headless
python3 tools/regress.py --exe ... --report  # show every measurement
python3 tools/regress.py --exe ... --bless   # rewrite the goldens
```

`--bless` overwrites the record of what the machine used to do. Only use it when
you can say why the change is right, and say so in the commit.

## Adding one

Write a generator in this directory using `asm.py`, emit a `.bin` and a `.meta`
into `tests/probes/`, and bless the golden. The `.meta` directives are:

```
flags       <headless flags>                  conditions the probe runs under
mct         <n>                               upper bound on the run
stop_at     <octal-addr>                      stop once this cell goes non-zero
measure     <name> <open> <close> <expected|?> a window in octal cells; '?' records
relation    <a> <b> b_longer_by_whole_mcts    an invariant between two windows
expect_mem  <name> <octal-addr> <octal-value> assert a computed result
expect_mem_min <name> <addr> <value>          ... at least
expect_mem_max <name> <addr> <value>          ... at most
dump        <octal-addr>[:len]                erasable to include in the golden
```

**Every probe needs sentinels or a `stop_at`**, and `regress.py` refuses to run
one that has neither. The AGC has no halt, so a finished probe parks in a
branch-to-itself — and a pure transfer-of-control loop trips the TC TRAP alarm
after about 10 ms, restarting the machine and running the whole probe again on
top of its own results. The interrupt probe did exactly that on its first run
and reported a counter 122 times too large.

Do not put `--mct` in `flags`: the frontend *adds* successive `--mct` values
rather than replacing them. Use the `mct` directive.

Prefer `measure` with a real expected value over `?`. A golden catches change; an
expectation catches *being wrong*, which is the harder and more useful thing. Use
`?` only where the reference genuinely does not state a figure, and say in the
generator's docstring why.
