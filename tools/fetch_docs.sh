#!/usr/bin/env bash
# Re-download the hardware references into docs/references/.
#
# Most of these are committed, because they are what the core cites and you want
# them to hand. Some are not:
#
#   - agcis_32_blk2_instructions.pdf is 94 MB, close enough to GitHub's 100 MB
#     hard limit to be a liability, so it is gitignored and fetched on demand.
#   - block2-schematics/ is 52 sheets mirrored from klabs.org.
#   - the-apollo-guidance-computer_jp2/ is 20 MB of page scans from archive.org.
#
# One reference this script deliberately does *not* fetch: Frank O'Brien's "The
# Apollo Guidance Computer: Architecture and Operation" is a current commercial
# book, not a government-funded report we may mirror. See
# docs/references/README.md.
#
# The script is idempotent: it skips anything already present, so running it
# after a fresh clone gets you the missing pieces and nothing else.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
refs="$root/docs/references"
ibiblio="https://www.ibiblio.org/apollo"
klabs="https://klabs.org/history/ech/agc_schematics"

get() { # get <url> <destination>
    local url=$1 dest=$2
    if [[ -s $dest ]]; then
        printf '  have %s\n' "${dest#"$refs"/}"
        return
    fi
    mkdir -p "$(dirname "$dest")"
    printf '  get  %s\n' "${dest#"$refs"/}"
    curl -sSfL -o "$dest" "$url" || { rm -f "$dest"; echo "    FAILED: $url" >&2; }
}

echo "Control pulses and instruction timing:"
get "$ibiblio/Documents/AgcPulsesAndSequences.txt" \
    "$refs/AgcPulsesAndSequences.txt"
get "$ibiblio/hrst/archive/1689.pdf" \
    "$refs/AGC4-Memo-9-Block-II-Instructions.pdf"
get "$ibiblio/hrst/archive/1008.pdf" \
    "$refs/AGC4-Logical-Description-1963.pdf"
get "$ibiblio/NARA-SW/E-2052.pdf" \
    "$refs/E-2052-AGC4-Basic-Training-Manual.pdf"

echo "Whole-machine reports:"
get "$ibiblio/Documents/R-700.pdf" \
    "$refs/R-700.pdf"

# Alonso & Hopkins, "The Apollo Guidance Computer" (R-393, 1963), as 40 page
# scans on archive.org. Gitignored for size; the item also carries a 3.4 MB PDF
# of the same pages if you would rather have one file.
echo "Alonso & Hopkins R-393 page scans (archive.org):"
ia_item="the-apollo-guidance-computer"
scans="$refs/${ia_item}_jp2"
if compgen -G "$scans/*.jp2" >/dev/null; then
    printf '  have %s\n' "${scans#"$refs"/}"
elif ! command -v unzip >/dev/null; then
    echo "  skip ${scans#"$refs"/}: needs unzip" >&2
else
    get "https://archive.org/download/$ia_item/${ia_item}_jp2.zip" "$scans.zip"
    if [[ -s $scans.zip ]]; then
        unzip -qo "$scans.zip" -d "$refs" && rm -f "$scans.zip"
    fi
fi

echo "Apollo Guidance Computer Information Series:"
series=(
    agcis_0_preface
    agcis_2_machine_instructions
    agcis_3_central_processor
    agcis_4_erasable
    agcis_5_timer_sequence_generator
    agcis_7_sequence_generator
    agcis_8_priority_control
    agcis_10_fixed
    agcis_16_fresh_start
    agcis_17_keyrupt_partial
    agcis_17_errata
    agcis_21_system_test
    agcis_30_block_ii_agc
    agcis_30_blk2_appendix_a
    agcis_30_blk2_appendix_b
    agcis_30_errata
    agcis_32_blk2_instructions          # 94 MB, gitignored — the reason this script exists
    agcis_32_blk2_instructions_errata1
)
for doc in "${series[@]}"; do
    get "$ibiblio/Documents/$doc.pdf" "$refs/agc-information-series/$doc.pdf"
done

echo "Block II NOR-gate schematics (klabs.org):"
sch="$refs/block2-schematics"
get "$klabs/" "$sch/index.html"
if [[ -s $sch/index.html ]]; then
    # The index references each sheet twice, as a thumbnail and as a full-size
    # image; take the full-size ones only.
    grep -oiE '(href|src)="logic/[^"]+\.jpg"' "$sch/index.html" \
        | sed 's/.*="//;s/"//' | grep -v '_small' | sort -u \
        | while read -r sheet; do get "$klabs/$sheet" "$sch/$sheet"; done
fi

echo "Done. See docs/references/README.md for what each document is for."
