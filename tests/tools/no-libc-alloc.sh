#!/bin/sh
# no-libc-alloc.sh - D-038: FultaArc's own code allocates only through proven allocators (src/core/fa_alloc.h).
# Fails when a C-library allocation call appears under src/ (vendor/ is upstream's and has its own hooks).
set -eu
cd "$(dirname "$0")/../.."
hits=$(grep -rnE '(^|[^A-Za-z0-9_.>])(malloc|calloc|realloc|free|strdup|strndup|aligned_alloc|posix_memalign)[[:space:]]*\(' \
        src --include='*.c' --include='*.h' \
    | grep -vE '^[^:]+:[0-9]+:[[:space:]]*(/?\*|//)' || true)
if [ -n "$hits" ]; then
    printf '%s\n' "no-libc-alloc: C-library allocation in src/ (use fa_alloc/fa_resize/fa_release, D-038):" "$hits" >&2
    exit 1
fi
printf '%s\n' "no-libc-alloc: src/ allocates only through proven allocators"
