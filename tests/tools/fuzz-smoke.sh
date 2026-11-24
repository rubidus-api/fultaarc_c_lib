#!/bin/sh
# fuzz-smoke.sh - a short libFuzzer run over FultaArc's readers (D-041): every saved finding
# (FIXTURES/regression/fuzz) and the small fixtures, each as a reading input and as a convert input.
#
#   sh tests/tools/fuzz-smoke.sh [seconds]      (after `make fuzz`; default 120 s)
#
# Corpus files are overwritten in place, never deleted; findings collect in build/fuzz/smoke/found, and any
# file there makes this script fail.
set -eu
secs=${1:-120}
fix=${FULTA_ARC_FIXTURES:-ops/fixtures}
corpus=build/fuzz/smoke/corpus
found=build/fuzz/smoke/found
mkdir -p "$corpus" "$found"
if [ -d "$fix/regression/fuzz" ]; then cp "$fix"/regression/fuzz/* "$corpus"/; fi
if [ -d "$fix" ]; then
    find "$fix" -type f -size -16k ! -name '*.md' ! -name '*.txt' ! -name '*.py' | head -400 | while read -r f; do
        n=$(basename "$f")
        printf '\000' | cat - "$f" > "$corpus/$n.r"       # knob 0x00: read and extract
        printf '\041' | cat - "$f" > "$corpus/$n.c"       # knob 0x21: with a password, converted to ZIP
    done
fi
before=$(ls "$found" | wc -l)
build/fuzz/fuzz_open -max_total_time="$secs" -timeout=30 -rss_limit_mb=2500 -malloc_limit_mb=2500 \
    -max_len=131072 -artifact_prefix="$found/" "$corpus" 2>&1 | tail -3
after=$(ls "$found" | wc -l)
if [ "$after" -gt "$before" ]; then echo "fuzz-smoke: new findings in $found"; exit 1; fi
echo "fuzz-smoke: ok ($secs s, no findings)"
