#!/usr/bin/env python3
"""A unit-delay evaluator for Mike Stewart's gate-level AGC netlists.

`ext/agc_simulation` is a Verilog netlist generated from the Block II module
schematics: every part is a 7400-series NOR pack, hex inverter or open-drain
buffer, wired by name.  Running it normally needs Icarus Verilog plus the whole
machine (memory, registers, a rope).  For settling a *cross-point* question —
"which control pulses does subinstruction X assert at timing pulse N?" — that is
far more machine than the question needs, and it is not reproducible on a host
without iverilog.

This module loads the netlist directly and evaluates it as what it is: a mesh of
NOR gates with a uniform propagation delay.  Only three primitives exist in the
whole netlist, and each is transcribed here from its own source file:

  nor_1/2/3/4  `assign #delay y = (rst) ? iv : ~(a|b|...)`   components/nor_N.v
  od_buf       `assign #delay y = (a == od_value) ? 1'bZ : 1'b0`  components/od_buf.v
  pullup       a weak 1, overridden by any driver

Every 74xx part in `components/` is a flat bundle of those, so the parser reads
the part definitions rather than hard-coding pinouts: pin order comes from the
component's own port list, and the netlist instantiates positionally.

Simulation is synchronous unit-delay — every gate recomputes from the previous
state each step, which is the same uniform-delay model the Verilog uses (all
gates carry `delay = 9`).  Cross-point outputs are levels that last a whole
timing pulse, so the caller sets inputs, runs the mesh until it stops changing,
and reads the settled state.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

# --- netlist model ----------------------------------------------------------


@dataclass(frozen=True)
class NorGate:
    """`y = ~(a | b | ...)`, or the reset value while rst is held."""

    out: str
    ins: tuple[str, ...]
    reset_value: int


@dataclass(frozen=True)
class OpenDrain:
    """Pulls `out` low unless its input sits at `od_value`; otherwise floats."""

    out: str
    inp: str
    od_value: int


@dataclass
class Netlist:
    nors: list[NorGate] = field(default_factory=list)
    ods: list[OpenDrain] = field(default_factory=list)
    pullups: set[str] = field(default_factory=set)
    modules: list[str] = field(default_factory=list)

    @property
    def nets(self) -> set[str]:
        seen: set[str] = set(self.pullups)
        for g in self.nors:
            seen.add(g.out)
            seen.update(g.ins)
        for o in self.ods:
            seen.add(o.out)
            seen.add(o.inp)
        return seen

    def driven_nets(self) -> set[str]:
        return (
            {g.out for g in self.nors} | {o.out for o in self.ods} | set(self.pullups)
        )

    def undriven_nets(self) -> set[str]:
        return self.nets - self.driven_nets()


# --- Verilog parsing --------------------------------------------------------

_COMMENT = re.compile(r"//[^\n]*")
_MODULE_HEAD = re.compile(r"\bmodule\s+(\w+)\s*\(([^;]*?)\)\s*;", re.S)
_LOCALPARAM = re.compile(r"\blocalparam\s+(\w+)\s*=\s*1'b([01])\s*;")
_PARAM_DECL = re.compile(r"\bparameter\s+(\w+)\s*=\s*1'b([01])\s*;")
_INSTANCE = re.compile(r"\b(\w+)\s*(#\(([^)]*)\)\s*)?(\w+)\s*\(([^;]*?)\)\s*;", re.S)

_NOT_AN_INSTANCE = {"module", "endmodule", "assign", "always", "input", "output"}


def _strip_comments(text: str) -> str:
    return _COMMENT.sub("", text)


def _split_args(arg_text: str) -> list[str]:
    """Positional connection list; an empty slot means the pin is unconnected."""
    return [a.strip() for a in arg_text.split(",")]


@dataclass(frozen=True)
class Component:
    """A 74xx part: its pin order, plus the primitives it bundles."""

    name: str
    ports: tuple[str, ...]
    nors: tuple[tuple[str, tuple[str, ...], str], ...]  # out, ins, reset-value param
    ods: tuple[tuple[str, str, int], ...]  # out, in, od_value
    params: tuple[str, ...]  # declared parameter order (ic1, ic2, ...)


def parse_component(path: Path) -> Component:
    text = _strip_comments(path.read_text())
    head = _MODULE_HEAD.search(text)
    if head is None:
        raise ValueError(f"{path}: no module declaration")
    name = head.group(1)
    ports = tuple(p.strip() for p in head.group(2).split(","))
    body = text[head.end() :]

    params = tuple(m.group(1) for m in _PARAM_DECL.finditer(body))
    consts = {m.group(1): int(m.group(2)) for m in _LOCALPARAM.finditer(body)}

    nors: list[tuple[str, tuple[str, ...], str]] = []
    ods: list[tuple[str, str, int]] = []
    for m in _INSTANCE.finditer(body):
        kind, _, inst_params, _inst, args = m.groups()
        if kind in _NOT_AN_INSTANCE:
            continue
        conn = _split_args(args)
        if kind.startswith("nor_"):
            # nor_N(y, a, ..., rst, clk): rst is `vrst`, which is 0 whenever the
            # part is powered and SIM_RST is low — the only condition we run in.
            out, ins = conn[0], tuple(conn[1:-2])
            plist = [p.strip() for p in (inst_params or "").split(",")]
            reset_param = plist[1] if len(plist) > 1 else "0"
            nors.append((out, ins, reset_param))
        elif kind == "od_buf":
            plist = [p.strip() for p in (inst_params or "").split(",")]
            od_name = plist[1] if len(plist) > 1 else "od_value"
            ods.append((conn[0], conn[1], consts[od_name]))
        else:
            raise ValueError(f"{path}: unexpected primitive {kind!r}")

    return Component(name, ports, tuple(nors), tuple(ods), params)


def load_components(components_dir: Path) -> dict[str, Component]:
    parts: dict[str, Component] = {}
    for path in sorted(components_dir.glob("*.v")):
        try:
            comp = parse_component(path)
        except (ValueError, KeyError, IndexError):
            continue  # memory parts and the like: not gate bundles, never used here
        parts[comp.name] = comp
    return parts


def load_module(path: Path, parts: dict[str, Component], into: Netlist) -> None:
    """Flatten one schematic module's instances into `into`.

    Net names are already globally unique in this netlist — internal nets carry
    a per-module prefix (`net_U6xxx`, `__A06_1__`) and ports are wired by
    identical names at the top level — so flattening by name is exact.
    """
    text = _strip_comments(path.read_text())
    head = _MODULE_HEAD.search(text)
    if head is None:
        raise ValueError(f"{path}: no module declaration")
    into.modules.append(head.group(1))
    body = text[head.end() :]

    unconnected = 0
    for m in _INSTANCE.finditer(body):
        kind, _, inst_params, inst, args = m.groups()
        if kind in _NOT_AN_INSTANCE:
            continue
        conn = _split_args(args)
        if kind == "pullup":
            into.pullups.add(conn[0])
            continue
        part = parts.get(kind)
        if part is None:
            raise ValueError(f"{path}: unknown part {kind!r} at {inst}")

        pin: dict[str, str] = {}
        for port, net in zip(part.ports, conn, strict=True):
            if not net:
                unconnected += 1
                net = f"__nc_{inst}_{port}"
            pin[port] = net
        # `vrst` is the internal power-on-reset term; `vcc`/`gnd` are supplies.
        pin.setdefault("vrst", "GND")

        values = [p.strip() for p in (inst_params or "").split(",") if p.strip()]
        ic = {name: 0 for name in part.params}
        for name, value in zip(part.params, values, strict=False):
            ic[name] = int(value.split("b")[-1])

        for out, ins, reset_param in part.nors:
            into.nors.append(
                NorGate(
                    out=pin[out],
                    ins=tuple(pin[i] for i in ins),
                    reset_value=ic.get(reset_param, 0),
                )
            )
        for out, inp, od_value in part.ods:
            into.ods.append(OpenDrain(out=pin[out], inp=pin[inp], od_value=od_value))


# --- simulation -------------------------------------------------------------


class Simulator:
    """Synchronous unit-delay evaluation of a flattened netlist."""

    #: Nets never driven by a gate default by naming convention, exactly as the
    #: upstream testbench declares them: `X_n` is active low, `X` active high.
    def __init__(self, netlist: Netlist, drives: dict[str, int] | None = None) -> None:
        self.netlist = netlist
        self.values: dict[str, int] = {}
        self.drives: dict[str, int] = {}

        self._strong: dict[str, list[NorGate]] = {}
        for gate in netlist.nors:
            self._strong.setdefault(gate.out, []).append(gate)
        self._open: dict[str, list[OpenDrain]] = {}
        for od in netlist.ods:
            self._open.setdefault(od.out, []).append(od)

        # Every open-drain net in this machine is a wired-AND with a pull-up,
        # but the resistor is drawn on whichever module the designer had room
        # on — WA_n is pulled up on A6 while Z16_n is pulled up over on A11.
        # Loading a subset of the modules therefore picks up open-drain drivers
        # whose pull-up is out of scope, and without one such a net would latch
        # low the first time anything pulled it and never recover.
        self._pulled_up = set(netlist.pullups) | {
            net for net in self._open if net not in self._strong
        }

        for net in netlist.nets:
            self.values[net] = 1 if net.endswith("_n") else 0
        for gate in netlist.nors:
            self.values[gate.out] = gate.reset_value

        self.drive(**(drives or {}))

    def drive(self, **nets: int) -> None:
        """Hold nets at a value, overriding their drivers (a testbench force)."""
        for name, value in nets.items():
            if name not in self.values:
                raise KeyError(f"no such net: {name}")
            self.drives[name] = value
            self.values[name] = value

    def release(self, *names: str) -> None:
        for name in names:
            self.drives.pop(name, None)

    def _step(self) -> bool:
        prev = self.values
        nxt = dict(prev)
        for net, gates in self._strong.items():
            if net in self.drives:
                continue
            value = 0
            for gate in gates:
                value = 0 if any(prev[i] for i in gate.ins) else 1
            nxt[net] = value
        for net, buffers in self._open.items():
            if net in self.drives or net in self._strong:
                continue
            low = any(prev[od.inp] != od.od_value for od in buffers)
            nxt[net] = 0 if low else (1 if net in self._pulled_up else prev[net])
        changed = nxt != prev
        self.values = nxt
        return changed

    def settle(
        self,
        max_steps: int = 200,
        witness: int = 40,
        known_ringing: frozenset[str] = frozenset(),
    ) -> frozenset[str]:
        """Run the mesh to rest; returns the nets that never came to rest.

        A few nets legitimately never settle here: an SR latch whose set and
        reset terms are both quiet holds its state in hardware, but a
        zero-timing evaluation of the same loop with no strobe can ring.  Rather
        than pretend otherwise, the simulator reports the ringing region and
        leaves it to the caller to assert that the nets it reads are outside it
        (see `Simulator.read`).

        `known_ringing` names nets a previous run already found to ring, which
        lets the loop stop as soon as everything *else* is at rest instead of
        burning the full step budget on a latch that will never converge.  It
        only ever shortens the run: the returned set still describes this run.
        """
        quiet = 0
        for _ in range(max_steps):
            changed = self._changed_nets()
            if not changed:
                return frozenset()
            if changed <= known_ringing:
                quiet += 1
                if quiet >= 2:
                    return known_ringing
            else:
                quiet = 0
        unstable: set[str] = set()
        for _ in range(witness):
            unstable |= self._changed_nets()
        return frozenset(unstable)

    def _changed_nets(self) -> set[str]:
        prev = self.values
        self._step()
        return {net for net in self.values if self.values[net] != prev[net]}

    def read(self, nets: list[str], unstable: frozenset[str]) -> dict[str, int]:
        """Sample nets, refusing to report a value from the unsettled region."""
        ringing = [net for net in nets if net in unstable]
        if ringing:
            raise RuntimeError(f"nets never settled: {', '.join(sorted(ringing))}")
        return {net: self.values[net] for net in nets}

    def __getitem__(self, net: str) -> int:
        return self.values[net]


def build(sim_root: Path, module_names: list[str]) -> Netlist:
    parts = load_components(sim_root / "components")
    netlist = Netlist()
    for name in module_names:
        load_module(sim_root / "modules" / f"{name}.v", parts, netlist)
    return netlist
