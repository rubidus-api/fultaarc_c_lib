#!/bin/sh
# rar15-pin.sh - check that src/rar/rar15/ is still byte-identical to the RAR 1.5 clean room's deliverable.
#
# src/rar/rar15/ holds rar15arc.c/.h and unpack15.c/.h copied unmodified from the clean room
# fultaarc-cleanroom-rar15 (final room HEAD b9fd1c0; the room is kept in FultaArc's private record, D-027).
# src/rar/rar15.sha256 holds the SHA-256 of each copied file, checked equal to the room's final commit when they were
# copied (D-029). FultaArc adapts the reader only from outside (src/rar/rar15_glue.c). A change to these files is a
# change to clean-room output: make it in a reopened room, record it there, then copy and re-pin here.
# Provenance: docs/provenance/rar15/.
#
# Two checks: every pinned file has its pinned hash, and the folder holds exactly the pinned files (an added file
# matters: the Makefile compiles every src/rar/rar15/*.c). Hidden files and editor backups (*~, *.swp) are ignored.
# Run by `make test`; exit status 1 on any difference.
set -eu
cd "$(dirname "$0")/../../src/rar/rar15"
if command -v sha256sum >/dev/null 2>&1; then
  sha256sum -c ../rar15.sha256 >/dev/null
else
  shasum -a 256 -c ../rar15.sha256 >/dev/null
fi
want=$(sed 's/^[0-9a-f]*  //' ../rar15.sha256 | sort)
have=$(find . -type f ! -name '.*' ! -name '*~' ! -name '*.swp' | sed 's|^\./||' | sort)
if [ "$want" != "$have" ]; then
  echo "rar15-pin: src/rar/rar15/ does not hold exactly the pinned files" >&2
  echo "pinned:" >&2; echo "$want" >&2
  echo "present:" >&2; echo "$have" >&2
  exit 1
fi
echo "rar15-pin: src/rar/rar15/ matches the clean room's final state ($(echo "$want" | wc -l | tr -d ' ') files)"
cd ../rar15_enc
if command -v sha256sum >/dev/null 2>&1; then
  sha256sum -c ../rar15_enc.sha256 >/dev/null
else
  shasum -a 256 -c ../rar15_enc.sha256 >/dev/null
fi
echo "rar15-pin: src/rar/rar15_enc/ matches fultaarc-cleanroom-rar15-enc3 (1 file)"
