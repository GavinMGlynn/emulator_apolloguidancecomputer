#!/usr/bin/env python3
"""Generate src/core/cpu/subinst_tables.c from the control-pulse reference model.

The authority for the pulse sequences is AGC4 Memo #9, transcribed in
docs/references/AgcPulsesAndSequences.txt.  The memo has known typos and omits
a handful of pulses that the hardware asserts implicitly, so the *corrected*
reading lives in ext/agcplusplus (MIT), a control-pulse-level model developed
with Mike Stewart.  Transcribing 57 subinstructions by hand would introduce
exactly the class of error the tables exist to prevent, so we parse them.

This runs once (or after an ext/agcplusplus bump); its output is committed as
ordinary source.  Every divergence from the memo it finds is reported, and each
one is expected to be accounted for in tools/oracle/FINDINGS.md.

Usage:  python3 tools/gen_subinst_tables.py [--check]
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "ext/agcplusplus/src/block2"
OUT = ROOT / "src/core/cpu/subinst_tables.c"
MEMO = ROOT / "docs/references/AgcPulsesAndSequences.txt"

# Pulses that are composites or implicit hardware signals rather than entries in
# the memo's own pulse list.  They still occupy a slot in a sequence.
IMPLICIT = {"_1xp10": "P1XP10", "_8xp5": "P8XP5"}

# Must match AGC_MAX_PULSES_PER_TIMEPULSE in src/core/cpu/subinst.h.  The memo
# says "zero to five control pulses"; we assert that rather than assume it.
AGC_MAX_PULSES = 6

# A few sequences poke machine state directly rather than through a named pulse,
# because the hardware signal has no mnemonic in the memo.  Map each to a
# synthetic pulse so it survives into the tables; an unrecognised one is a hard
# error rather than a silent omission (RESUME's `iip = false` was dropped
# exactly that way once, and the machine hung with an interrupt permanently in
# progress until RUPT LOCK restarted it).
BARE_ASSIGNMENTS = {
    ("iip", "false"): "clriip",
}

# Places where the *gates* overrule the reference model, applied after parsing.
# ext/agcplusplus is the corrected reading of the memo, but it is still a model,
# and where it and the memo disagree the netlists in ext/agc_simulation and the
# original MIT drawings decide. Each entry names the drawing that settles it and
# carries a FINDINGS row; `tools/oracle/gate_crosspoint.py` prints the timeline
# it is derived from.
#
# Format: subinstruction -> list of (timing pulse, pulses to drop, pulses to add).
GATE_CORRECTIONS: dict[str, list[tuple[int, set[str], list[str]]]] = {
    # FINDINGS #11. AGCPlusPlus moves NEACOF from MP3 T6 to T12 to keep the
    # end-around carry inhibited through the multiply's final sum. The hardware
    # does not: cross-point drawing 2005263 clears the NEAC latch at T6 (via
    # TL15, exactly where the memo prints NEACOF), and service-gates drawing
    # 2005252 gate 33457 covers the rest of MP3 with a separate MP3A term on
    # the carry. We put the pulse back where both the memo and the cross-point
    # gates have it and model MP3A in the adder (src/core/cpu/cpu.c).
    "mp3": [
        (6, set(), ["neacof"]),
        (12, {"neacof"}, []),
    ],
}


def apply_gate_corrections(parsed: dict) -> None:
    for name, edits in GATE_CORRECTIONS.items():
        rows = parsed.get(name)
        if rows is None:
            raise SystemExit(f"gate correction names {name!r}, which is not a subinstruction")
        for tp, drop, add in edits:
            variants = rows.get(tp, [])
            for i, (mask, value, pulses) in enumerate(list(variants)):
                kept = [p for p in pulses if p not in drop]
                if kept == pulses and not add:
                    raise SystemExit(
                        f"gate correction for {name} T{tp} changes nothing; "
                        f"the reference model may already agree — re-check FINDINGS"
                    )
                variants[i] = (mask, value, kept + [p for p in add if p not in kept])
            if not variants and add:
                rows[tp] = [(0b00, 0b00, list(add))]
            rows[tp] = [r for r in rows.get(tp, []) if r[2]]
            if not rows[tp]:
                del rows[tp]


def pulse_enum(name: str) -> str:
    return "AGC_P_" + IMPLICIT.get(name, name.upper())


# --- Parsing the reference model ---------------------------------------------


def split_top_level_cases(body: str) -> list[tuple[list[str], str]]:
    """Split a switch body into [(case labels, statements)] at brace depth 0."""
    out: list[tuple[list[str], str]] = []
    labels: list[str] = []
    buf: list[str] = []
    depth = 0
    for line in body.splitlines():
        stripped = line.strip()
        m = re.match(r"case\s+([^:]+):\s*$", stripped)
        if depth == 0 and m:
            if buf:
                out.append((labels, "\n".join(buf)))
                labels, buf = [], []
            labels.append(m.group(1).strip())
            continue
        depth += line.count("{") - line.count("}")
        buf.append(line)
    if buf:
        out.append((labels, "\n".join(buf)))
    return out


def extract_block(text: str, start: int) -> tuple[str, int]:
    """Return the {...} block beginning at or after `start`, and the index past it."""
    open_idx = text.index("{", start)
    depth = 0
    for i in range(open_idx, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_idx + 1 : i], i + 1
    raise ValueError("unbalanced braces")


# Pulses that write the branch registers. A `switch (cpu.br)` positioned after
# one of these in the same timepulse is testing the value that pulse just
# produced — it is NOT a condition on the branch registers as they stood when
# the timepulse began, and it must not be hoisted into a row condition.
BR_WRITERS = {"tsgn", "tsgn2", "tsgu", "tmz", "tov", "tpzg", "tl15"}

# Pulses that re-test the branch registers themselves, so they are safe to issue
# unconditionally: each decides internally whether to act. This is what lets a
# late conditional be flattened into the union of its arms.
SELF_CONDITIONAL = {"clxc", "rb1f"}


def eval_body(body: str, br: int) -> list[str]:
    """Walk a timepulse body for one BR value, returning the ordered pulse list.

    Handles pulse calls, `switch (cpu.br)` groups (which may appear mid-list, as
    they do in DV1 T2), and ignores `break`.

    A branch switch that follows a BR-writing pulse is a *late* conditional: the
    hardware evaluates it against the value just computed, whereas a row
    condition is evaluated before the timepulse runs at all. Those are flattened
    to the union of both arms, which is only sound because every pulse involved
    re-tests the branch registers itself.
    """
    pulses: list[str] = []
    br_written = False
    i = 0
    while i < len(body):
        m = re.compile(r"switch\s*\(\s*cpu\.br\s*\)").search(body, i)
        call = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*cpu\s*\)\s*;").search(body, i)
        bare = re.compile(r"\bcpu\.([a-z_]+)\s*=\s*([A-Za-z0-9_]+)\s*;").search(body, i)
        if bare and (not m or bare.start() < m.start()) and (not call or bare.start() < call.start()):
            key = (bare.group(1), bare.group(2))
            if key not in BARE_ASSIGNMENTS:
                raise SystemExit(
                    f"unmapped bare state assignment cpu.{key[0]} = {key[1]}; "
                    f"add it to BARE_ASSIGNMENTS and give it a pulse in pulses.c"
                )
            pulses.append(BARE_ASSIGNMENTS[key])
            i = bare.end()
            continue
        if m and (not call or m.start() < call.start()):
            block, end = extract_block(body, m.end())
            arms = split_top_level_cases(block)
            if br_written:
                # Late conditional: emit the union of the arms, in body order,
                # and let each pulse re-test BR for itself.
                seen: list[str] = []
                for _, stmts in arms:
                    for p in eval_body(stmts, br):
                        if p in seen:
                            continue
                        if p not in SELF_CONDITIONAL:
                            raise SystemExit(
                                f"late branch switch selects {p!r}, which does not "
                                f"re-test BR itself; it cannot be flattened. Teach "
                                f"the executor a mid-timepulse condition instead."
                            )
                        seen.append(p)
                pulses += seen
            else:
                for labels, stmts in arms:
                    values = {int(v.replace("0b", ""), 2) for v in labels}
                    if br in values:
                        pulses += eval_body(stmts, br)
                        break
            i = end
            continue
        if not call:
            break
        name = call.group(1)
        if name not in ("break",):
            pulses.append(name)
            if name in BR_WRITERS:
                br_written = True
        i = call.end()
    return pulses


def parse_subinstructions() -> dict[str, dict[int, list[tuple[int, int, list[str]]]]]:
    text = (SRC / "subinstructions.cpp").read_text()
    result: dict[str, dict[int, list[tuple[int, int, list[str]]]]] = {}
    for m in re.finditer(r"^void\s+([a-z0-9_]+)\s*\(\s*Cpu&\s*cpu\s*\)\s*\{", text, re.M):
        name = m.group(1)
        body, _ = extract_block(text, m.start())
        tp_switch = re.search(r"switch\s*\(\s*cpu\.timepulse\s*\)", body)
        if not tp_switch:
            continue
        tp_block, _ = extract_block(body, tp_switch.end())
        rows: dict[int, list[tuple[int, int, list[str]]]] = {}
        for labels, stmts in split_top_level_cases(tp_block):
            for label in labels:
                tp = int(label)
                # Collapse the four BR outcomes into the fewest (mask, value) rows.
                by_br = {br: eval_body(stmts, br) for br in range(4)}
                rows[tp] = collapse(by_br)
        result[name] = rows
    return result


def collapse(by_br: dict[int, list[str]]) -> list[tuple[int, int, list[str]]]:
    """Turn {br -> pulses} into the fewest (mask, value, pulses) rows.

    Mirrors how the memo writes conditions: `xx` (always), `0x`/`1x` (BR1 only),
    `x0`/`x1` (BR2 only), or a fully specified pair.
    """
    if len({tuple(v) for v in by_br.values()}) == 1:
        return [(0b00, 0b00, by_br[0])] if by_br[0] else []
    rows: list[tuple[int, int, list[str]]] = []
    covered: set[int] = set()
    for mask, value in ((0b10, 0b00), (0b10, 0b10), (0b01, 0b00), (0b01, 0b01)):
        group = [br for br in range(4) if (br & mask) == value]
        if any(br in covered for br in group):
            continue
        if len({tuple(by_br[br]) for br in group}) == 1:
            rows.append((mask, value, by_br[group[0]]))
            covered.update(group)
    for br in range(4):
        if br not in covered:
            rows.append((0b11, br, by_br[br]))
    return [r for r in rows if r[2]]


def parse_dispatch() -> list[tuple[str, int, bool, int, int]]:
    """Read the subinstruction dispatch list (stage, extend, SQ mask, SQ value)."""
    text = (SRC / "subinstructions.hpp").read_text()
    block = text[text.index("subinstruction_list[]") :]
    # Stop at the end of the array initialiser: the RUPT_/COUNT_ singletons that
    # follow it are involuntary sequences, injected by priority control rather
    # than decoded from SQ, and their all-zero mask would match every opcode.
    block = block[: block.index("};")]
    out = []
    for m in re.finditer(
        r"\{\s*(\d+),\s*(true|false),\s*(0[0-7]*),\s*(0[0-7]*),\s*\"([A-Z0-9]+)\"", block
    ):
        stage, ext, mask, op, name = m.groups()
        out.append((name, int(stage), ext == "true", int(mask, 8), int(op, 8)))
    return out


# --- Cross-checking against the memo -----------------------------------------


def parse_memo() -> dict[str, dict[int, list[list[str]]]]:
    """Loosely parse the memo's own tables so we can report where we differ."""
    memo: dict[str, dict[int, list[list[str]]]] = {}
    current = None
    for line in MEMO.read_text().splitlines():
        head = re.match(r"^([A-Z][A-Z0-9]*)\s*(\[[^]]*\])?\s*$", line.strip())
        if head and not line.startswith(" "):
            current = head.group(1)
            memo.setdefault(current, {})
            continue
        row = re.match(r"^\s*(\d{1,2})\.\s+(xx|[01xX]{2})\s+(.*)$", line)
        if row and current:
            tp = int(row.group(1))
            memo[current].setdefault(tp, []).append(row.group(3).split())
    return memo


def report_divergences(parsed, memo) -> list[str]:
    notes = []
    for name, rows in sorted(parsed.items()):
        key = name.upper()
        if key not in memo:
            notes.append(f"{key}: no memo table (involuntary/counter sequence)")
            continue
        ours = {tp for tp in rows}
        theirs = set(memo[key])
        for tp in sorted(theirs - ours):
            notes.append(f"{key} T{tp}: in memo, absent from the model")
        for tp in sorted(ours - theirs):
            notes.append(f"{key} T{tp}: in the model, absent from the memo")
        for tp in sorted(ours & theirs):
            model = {p.upper() for _, _, ps in rows[tp] for p in ps}
            paper = {p.upper() for row in memo[key][tp] for p in row}
            model -= {"P1XP10", "_1XP10", "_8XP5"}
            if model != paper:
                extra = ", ".join(sorted(model - paper)) or "-"
                missing = ", ".join(sorted(paper - model)) or "-"
                notes.append(f"{key} T{tp}: model adds [{extra}], memo has extra [{missing}]")
    return notes


# --- Emission -----------------------------------------------------------------


def emit(parsed, dispatch) -> str:
    widest = max(
        len(ps) for rows in parsed.values() for variants in rows.values() for _, _, ps in variants
    )
    if widest > AGC_MAX_PULSES:
        raise SystemExit(
            f"a sequence asserts {widest} pulses in one timing pulse; "
            f"raise AGC_MAX_PULSES_PER_TIMEPULSE (currently {AGC_MAX_PULSES}) in subinst.h"
        )

    lines = [
        "/* GENERATED by tools/gen_subinst_tables.py — do not edit by hand.",
        " *",
        " * The Block II control-pulse sequences: for every subinstruction, which",
        " * control pulses fire at which timing pulse T1-T12, under which branch",
        " * register condition.  Transcribed from AGC4 Memo #9",
        " * (docs/references/AgcPulsesAndSequences.txt) as corrected by the",
        " * control-pulse reference model in ext/agcplusplus; divergences between the",
        " * two are catalogued in tools/oracle/FINDINGS.md.",
        " *",
        " * BR conditions use the memo's own encoding: br = (BR1 << 1) | BR2, with",
        " * br_mask selecting which of the two bits the row cares about (the memo's",
        " * 'x' is a zero mask bit).",
        " */",
        '#include "subinst.h"',
        "",
    ]

    for name, rows in sorted(parsed.items()):
        lines.append(f"static const agc_pulse_row {name}_rows[] = {{")
        for tp in sorted(rows):
            for mask, value, ps in rows[tp]:
                cond = "".join(
                    "x" if not (mask & bit) else ("1" if value & bit else "0")
                    for bit in (0b10, 0b01)
                )
                pl = ", ".join(pulse_enum(p) for p in ps)
                lines.append(
                    f"    {{ {tp:2d}, 0x{mask:x}, 0x{value:x}, {{ {pl} }} }},"
                    f"  /* T{tp} {cond} */"
                )
        lines.append("};")
        lines.append("")

    lines.append("const agc_subinst agc_subinst_table[] = {")
    seen: set[str] = set()
    for name, stage, ext, mask, op in dispatch:
        fn = name.lower()
        if fn not in parsed:
            continue
        lines.append(
            f'    {{ "{name}", {stage}, {"true" if ext else "false"}, 0{mask:02o}, 0{op:03o}, '
            f"{fn}_rows, (uint8_t)(sizeof {fn}_rows / sizeof *{fn}_rows) }},"
        )
        seen.add(fn)
    lines.append("};")
    lines.append(
        "const size_t agc_subinst_count = sizeof agc_subinst_table / sizeof *agc_subinst_table;"
    )
    lines.append("")

    # The involuntary sequences are not reachable through SQ/ST decoding; the
    # sequence generator injects them directly, so they get named handles.
    lines.append("/* Involuntary sequences, injected by priority control rather than decoded. */")
    for fn in sorted(set(parsed) - seen):
        lines.append(
            f"const agc_subinst agc_subinst_{fn} = "
            f'{{ "{fn.upper()}", 0, false, 0, 0, {fn}_rows, '
            f"(uint8_t)(sizeof {fn}_rows / sizeof *{fn}_rows) }};"
        )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="fail if the committed file is stale")
    args = ap.parse_args()

    if not (SRC / "subinstructions.cpp").exists():
        print("ext/agcplusplus is missing. Run: git submodule update --init ext/agcplusplus",
              file=sys.stderr)
        return 1

    parsed = parse_subinstructions()
    apply_gate_corrections(parsed)
    dispatch = parse_dispatch()
    text = emit(parsed, dispatch)

    for note in report_divergences(parsed, parse_memo()):
        print(f"memo divergence: {note}", file=sys.stderr)

    if args.check:
        if not OUT.exists() or OUT.read_text() != text:
            print(f"{OUT} is stale; re-run without --check", file=sys.stderr)
            return 1
        return 0

    OUT.write_text(text)
    print(f"wrote {OUT} ({len(parsed)} subinstructions)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
