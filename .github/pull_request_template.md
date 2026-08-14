<!-- CONTRIBUTING.md has the long version. This is the checklist. -->

## What this changes

<!-- One or two sentences. If it changes emulated behaviour, say which
     subinstruction, timing pulse or branch condition. -->

## Where the answer came from

<!-- The rule this project runs on: doubt is resolved from a source, not by
     adjusting a parameter until a test passes. Cite it — a memo page, a gate in
     ext/agc_simulation, a schematic, a rope listing. "Matches the reference
     model" is a fine answer. "It made the test pass" is not. -->

## Checklist

- [ ] `ctest --preset linux-debug` green
- [ ] `ctest --preset linux-release` green, and the goldens are unchanged — or
      the change to them is explained above and is the point of the PR
- [ ] Tests read as hardware facts, named as sentences
- [ ] `docs/PROJECT_STATUS.md` updated: what now works, and how it was verified
- [ ] `docs/COMPLETION_PLAN.md` updated: item ticked, and any tails found along
      the way added
- [ ] Temporary instrumentation reverted, including anything under `ext/`
- [ ] No instruction-level shortcut added to `src/core` — one tick is still one
      timing pulse
