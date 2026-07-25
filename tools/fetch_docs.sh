#!/usr/bin/env bash
# Re-download the hardware references into docs/references/.
#
# Most of these are committed, because they are what the core cites and you want
# them to hand. Two are not:
#
#   - agcis_32_blk2_instructions.pdf is 94 MB, close enough to GitHub's 100 MB
#     hard limit to be a liability, so it is gitignored and fetched on demand.
#   - block2-schematics/ is 52 sheets mirrored from klabs.org.
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
