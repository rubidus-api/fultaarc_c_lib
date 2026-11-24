#!/bin/sh
# run.sh - the CBMC proofs (D-041): each harness checks memory safety (bounds, pointers, signed overflow,
# division) with unwinding assertions, so a loop the bound cuts short is a failure, not a pass.
#   sh tests/proofs/run.sh [cbmc]            (CBMC 6.x; default: `cbmc` on PATH)
set -eu
CBMC=${1:-cbmc}
tmp=build/proofs
mkdir -p "$tmp"
pp() { gcc -std=gnu17 -E -P -include stdbool.h -include stdalign.h -Iinclude -Isrc -Ivendor/proven/include \
          -Ivendor/proven/platform -Ivendor/proven/crypto/include -Ivendor/lzma "$@" | sed -E 's/\[\[[a-z_:]+(\([^)]*\))?\]\]//g'; }
fail=0
# harness   bound-define   unwind   [per-loop bounds]
while read -r h def unwind extra; do
    [ -z "$h" ] && continue
    pp $def "tests/proofs/$h.c" > "$tmp/$h.i"
    if $CBMC "$tmp/$h.i" --function harness --bounds-check --pointer-check --signed-overflow-check \
            --div-by-zero-check --unwind "$unwind" ${extra:+--unwindset "$extra"} --unwinding-assertions --object-bits 14 > "$tmp/$h.log" 2>&1; then
        echo "prove: $h ($def): $(grep -Eo '\*\* [0-9]+ of [0-9]+' "$tmp/$h.log" | head -1 | sed 's/\*\* //') failed - ok"
    else
        echo "prove: $h ($def): FAILED (see $tmp/$h.log)"; fail=1
    fi
done <<LIST
sanitize          -DN=10  13
utf8_codepage     -DN=4   30
tar_cpio_numbers  -DN=12  16
sz_header         -DN=8   10  parse_folder.6:66,harness.2:66
LIST
exit $fail
