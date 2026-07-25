#!/usr/bin/env python3
"""A minimal Block II AGC assembler for bare-metal probes.

Deliberately not yaYUL. A probe is a few dozen hand-placed words whose *exact*
layout matters, and the whole point of it is to be readable next to the pulse
tables it is checking. Depending on the reference assembler to build the thing
that checks the reference model would defeat the exercise; this is a hundred
lines that emit words, with no interpreter, no banking machinery, and no
bugger words.

Probes live in fixed-fixed memory (04000-07777), which is addressed directly
with no bank register involved, so a probe never has to set up FB.

Output is the physical rope layout that `yaYUL --hardware` produces and that
src/core/memory reads: data bits 1-14 in positions 1-14, parity in position 15,
data bit 15 in position 16, odd parity across all sixteen.
"""
from __future__ import annotations

# --- Machine layout ----------------------------------------------------------

FIXED_WORDS = 36864
FIXED_FIXED_START = 0o4000
FIXED_FIXED_END = 0o7777

# Erasable regions a probe may use freely. 0-7 are the central registers,
# 010-023 are the special/editing registers, 024-060 are counter cells, and 067
# is the night watchman's cell.
SCRATCH_START = 0o100


class AsmError(Exception):
    pass


class Label:
    """A forward-declarable address."""

    def __init__(self, name: str):
        self.name = name
        self.addr: int | None = None

    def __int__(self) -> int:
        if self.addr is None:
            raise AsmError(f"label {self.name} was never placed")
        return self.addr

    def __repr__(self) -> str:
        return f"<{self.name}@{self.addr if self.addr is None else oct(self.addr)}>"


def _resolve(a) -> int:
    return int(a)


class Asm:
    """Assembles into fixed-fixed memory starting at `org`."""

    def __init__(self, org: int = FIXED_FIXED_START):
        if not FIXED_FIXED_START <= org <= FIXED_FIXED_END:
            raise AsmError(f"probes must live in fixed-fixed memory, not {oct(org)}")
        self.pc = org
        self._items: list[tuple[int, object]] = []  # (address, int | callable)

    # -- placement ------------------------------------------------------------

    def label(self, name: str) -> Label:
        lab = Label(name)
        lab.addr = self.pc
        return lab

    def here(self) -> int:
        return self.pc

    def at(self, addr: int) -> None:
        """Continue assembling at an absolute address."""
        if not FIXED_FIXED_START <= addr <= FIXED_FIXED_END:
            raise AsmError(f"{oct(addr)} is outside fixed-fixed memory")
        self.pc = addr

    def _emit(self, thunk) -> int:
        addr = self.pc
        if addr > FIXED_FIXED_END:
            raise AsmError("probe overflowed fixed-fixed memory")
        self._items.append((addr, thunk))
        self.pc += 1
        return addr

    def word(self, value) -> int:
        """A literal constant."""
        return self._emit(lambda: _resolve(value) & 0o77777)

    # -- operand encoders -----------------------------------------------------

    @staticmethod
    def _op(code: int, addr, width: int = 12) -> int:
        a = _resolve(addr)
        limit = (1 << width) - 1
        if not 0 <= a <= limit:
            raise AsmError(f"address {oct(a)} does not fit in {width} bits")
        return (code << 12) | a

    @staticmethod
    def _qc(code: int, quarter: int, addr) -> int:
        a = _resolve(addr)
        if not 0 <= a <= 0o1777:
            raise AsmError(f"address {oct(a)} does not fit in 10 bits")
        return (code << 12) | (quarter << 10) | a

    def _instr(self, fn) -> int:
        return self._emit(fn)

    # -- basic instructions ---------------------------------------------------
    # Order code in bits 13-15; quarter code, where there is one, in bits 11-12.

    def tc(self, a):    return self._instr(lambda: self._op(0, a))
    def ccs(self, a):   return self._instr(lambda: self._qc(1, 0, a))
    def tcf(self, a):   return self._instr(lambda: self._tcf(a))
    def das(self, a):   return self._instr(lambda: self._qc(2, 0, a))
    def lxch(self, a):  return self._instr(lambda: self._qc(2, 1, a))
    def incr(self, a):  return self._instr(lambda: self._qc(2, 2, a))
    def ads(self, a):   return self._instr(lambda: self._qc(2, 3, a))
    def ca(self, a):    return self._instr(lambda: self._op(3, a))
    def cs(self, a):    return self._instr(lambda: self._op(4, a))
    def index(self, a): return self._instr(lambda: self._qc(5, 0, a))
    def dxch(self, a):  return self._instr(lambda: self._qc(5, 1, a))
    def ts(self, a):    return self._instr(lambda: self._qc(5, 2, a))
    def xch(self, a):   return self._instr(lambda: self._qc(5, 3, a))
    def ad(self, a):    return self._instr(lambda: self._op(6, a))
    def mask(self, a):  return self._instr(lambda: self._op(7, a))

    def _tcf(self, a) -> int:
        """TCF is order code 001 with a non-zero quarter code — and the quarter
        code bits *are* address bits 11-12, so any fixed address encodes it and
        anything below 02000 would assemble as CCS instead."""
        addr = _resolve(a)
        if addr < 0o2000:
            raise AsmError(f"TCF {oct(addr)} would assemble as CCS")
        return self._op(1, addr)

    # -- pseudo-codes ---------------------------------------------------------
    # Not instructions: the hardware recognises these three *addresses* during
    # the fetch and turns the TC into a flag change. One MCT each.

    def relint(self): return self.tc(3)
    def inhint(self): return self.tc(4)
    def extend(self): return self.tc(6)

    def resume(self): return self.index(0o17)
    def noop(self):   return self.tcf(self.pc + 1)

    # -- extracodes -----------------------------------------------------------
    # Each must be preceded by EXTEND, which costs an MCT of its own.

    def dv(self, a):    return self._instr(lambda: self._qc(1, 0, a))
    def bzf(self, a):   return self._instr(lambda: self._ext_branch(1, a, "BZF"))
    def msu(self, a):   return self._instr(lambda: self._qc(2, 0, a))
    def qxch(self, a):  return self._instr(lambda: self._qc(2, 1, a))
    def aug(self, a):   return self._instr(lambda: self._qc(2, 2, a))
    def dim(self, a):   return self._instr(lambda: self._qc(2, 3, a))
    def dca(self, a):   return self._instr(lambda: self._op(3, a))
    def dcs(self, a):   return self._instr(lambda: self._op(4, a))
    def su(self, a):    return self._instr(lambda: self._qc(6, 0, a))
    def bzmf(self, a):  return self._instr(lambda: self._ext_branch(6, a, "BZMF"))
    def mp(self, a):    return self._instr(lambda: self._op(7, a))

    def read(self, ch):  return self._instr(lambda: 0o0000 | (_resolve(ch) & 0o77))
    def write(self, ch): return self._instr(lambda: 0o1000 | (_resolve(ch) & 0o77))

    def _ext_branch(self, code: int, a, name: str) -> int:
        addr = _resolve(a)
        if addr < 0o2000:
            raise AsmError(f"{name} {oct(addr)} needs a non-zero quarter code")
        return self._op(code, addr)

    # -- output ---------------------------------------------------------------

    def build(self) -> dict[int, int]:
        return {addr: (thunk() if callable(thunk) else thunk) & 0o77777
                for addr, thunk in self._items}

    def write_rope(self, path: str) -> None:
        image = bytearray(FIXED_WORDS * 2)
        for addr, value in self.build().items():
            raw = encode_hardware_word(value)
            image[addr * 2] = raw >> 8
            image[addr * 2 + 1] = raw & 0xFF
        with open(path, "wb") as f:
            f.write(image)


def encode_hardware_word(value: int) -> int:
    """15-bit word -> the physical rope layout, with odd parity."""
    raw = (value & 0o37777) | ((value & 0o40000) << 1)
    if bin(raw).count("1") % 2 == 0:
        raw |= 0o40000  # parity occupies position 15
    return raw
