#!/usr/bin/env bash
# Assemble the AGC core ropes we test against into roms/, and stage the
# physical rope-module dumps into bios/.
#
# Both sets come from the vendored Virtual AGC submodule (ext/virtualagc):
#   - roms/  : ropes assembled from the original MIT/Draper listings by yaYUL.
#              These are what the emulator loads as fixed memory.
#   - bios/  : dumps read out of real, physical core-rope modules. The AGC has
#              no BIOS in the PC sense — the rope IS the firmware — so these
#              hardware dumps are the closest analogue and are kept separate
#              from the assembled ropes because they carry real manufacturing
#              defects (bad strands, broken cores) we want to be able to boot.
#
# Neither directory is committed (see .gitignore); re-run this script to
# repopulate. Requires the ext/virtualagc submodule to be initialised.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
vagc="$root/ext/virtualagc"
scratch="$root/build/yayul"

if [[ ! -f "$vagc/yaYUL/yaYUL.c" ]]; then
    echo "ext/virtualagc is missing. Run: git submodule update --init ext/virtualagc" >&2
    exit 1
fi

# --- Build yaYUL --------------------------------------------------------------
# The upstream Makefile does not survive this toolchain (it mangles $(CC)), and
# we only need the one binary, so compile the translation units directly.
mkdir -p "$scratch"
yayul="$scratch/yaYUL"
if [[ ! -x "$yayul" ]]; then
    echo "Building yaYUL..."
    # Globbing the directory would break the day a submodule bump adds a second
    # file carrying a main() — the linker would fail on a duplicate symbol and
    # the message would say nothing about why. Take everything except the known
    # alternate entry points instead, so a *new* one is a clear error rather
    # than a silent inclusion.
    sources=()
    for f in "$vagc"/yaYUL/*.c; do
        case "$(basename "$f")" in
            # Not part of the assembler: separate tools that live in the same
            # directory and have main() of their own.
            Disassembler.c|dumpROM.c|oct2bin.c) continue ;;
            *) sources+=("$f") ;;
        esac
    done
    "${CC:-clang}" -O2 -w -DNVER= -DINSTALLDIR=/usr/local \
        -o "$yayul" "${sources[@]}" -lm || {
        echo "  yaYUL failed to build. If the error is a duplicate main(), a" >&2
        echo "  Virtual AGC bump has added a tool to yaYUL/; add it to the" >&2
        echo "  exclusion list above." >&2
        exit 1
    }
fi

# --- Assemble ropes -----------------------------------------------------------
# Chosen for what each one exercises; see docs/references/TEST_SOFTWARE.md.
ropes=(
    Validation      # Ed Smally's instruction validation suite — our amidog analogue
    Aurora12        # Block II development rope with a self-check program
    Retread50       # small early Block II rope, boots fast
    SundialE        # Block II erasable/fixed exerciser
    Luminary099     # Apollo 11 LM (the Eagle rope)
    Comanche055     # Apollo 11 CM
    Luminary131     # Apollo 13/14 LM
    LM131R1         # the build the Luminary131PlusLM131R1 module dump was read from
    Artemis072      # Apollo 15-17 CM
    Zerlina56       # late experimental LM rope
)

for rope in "${ropes[@]}"; do
    src="$vagc/$rope"
    [[ -d "$src" ]] || { echo "skip $rope (not in submodule)"; continue; }
    main="MAIN.agc"
    [[ -f "$src/$main" ]] || main="$rope.agc"
    [[ -f "$src/$main" ]] || { echo "skip $rope (no main source)"; continue; }

    # Each rope was built for a specific assembler dialect and checksum scheme
    # (--blk2, --early-sbank, --honeywell, --parity, --no-checksums ...). The
    # authoritative list is in the rope's own upstream Makefile; read it rather
    # than hardcoding, so a submodule bump can't silently mis-assemble a rope.
    args=(--unpound-page)
    if [[ -f "$src/Makefile" ]]; then
        # shellcheck disable=SC2207
        args+=($(sed -n 's/^EXTRA_YAYUL_ARGS *+*= *//p' "$src/Makefile" | tr '\n' ' '))
    fi
    # We always want --hardware, the *physical* rope bit layout: data bits 1-14
    # in positions 1-14, parity in position 15, data bit 15 in position 16.
    # That is what a real rope module presents to the sense amplifiers, so it is
    # what our fixed-memory model stores and parity-checks; reading it costs one
    # mask and one shift to reassemble the sign into bits 15 and 16.
    #
    # yaAGC's own --parity layout (parity in bit 1, data shifted up into bits
    # 2-16) is a software convenience and is NOT what we read. --hardware wins
    # over --parity inside yaYUL, but strip --parity anyway so the intent of the
    # command line is unambiguous.
    filtered=()
    for a in "${args[@]}"; do [[ $a == --parity ]] || filtered+=("$a"); done
    args=("${filtered[@]}" --hardware)

    out="$root/roms/$rope"
    mkdir -p "$out"
    echo "Assembling $rope/$main ${args[*]} ..."
    ( cd "$src" && "$yayul" "${args[@]}" "$main" >"$out/$rope.lst" 2>&1 ) || {
        echo "  FAILED — see $out/$rope.lst" >&2; continue; }
    if ! grep -qE '^Fatal errors( \(final\))?: +0' "$out/$rope.lst"; then
        echo "  FAILED (fatal assembler errors) — see $out/$rope.lst" >&2
        rm -f "$src/$main.bin" "$src/$main.symtab" "$src/$main.binsource"
        continue
    fi
    mv "$src/$main.bin"     "$out/$rope.bin"
    mv "$src/$main.symtab"  "$out/$rope.symtab" 2>/dev/null || true
    rm -f "$src/$main.binsource"
    printf '  -> roms/%s/%s.bin (%s bytes)\n' "$rope" "$rope" "$(stat -c%s "$out/$rope.bin")"
done

# --- Stage the physical rope-module dumps ------------------------------------
dumps="$vagc/Rope-Module Dump Library"
if [[ -d "$dumps" ]]; then
    mkdir -p "$root/bios/rope-modules"
    # Block II only: this emulator is a Block II machine.
    find "$dumps" -name '*BLK2*.bin' -exec cp -n {} "$root/bios/rope-modules/" \;
    cp -n "$vagc/LM131R1/Luminary131PlusLM131R1ModuleDump.bin" \
          "$root/bios/rope-modules/" 2>/dev/null || true
    cp -n "$dumps/README.md" "$root/bios/rope-modules/UPSTREAM-README.md" 2>/dev/null || true
    echo "Staged $(ls -1 "$root/bios/rope-modules" | wc -l) rope-module dumps in bios/rope-modules/"
fi
