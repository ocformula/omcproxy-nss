#!/bin/sh
# Build and run the host unit tests. Run from the repository root.
set -e

CC=${CC:-gcc}
CFLAGS="-std=c99 -D_GNU_SOURCE -Wall -Wextra -Werror -Isrc"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

$CC $CFLAGS tests/test_mcs_table.c src/mcs_table.c -o "$OUT/test_mcs_table"
"$OUT/test_mcs_table"

$CC $CFLAGS tests/test_mcs_nl.c src/mcs_nl.c -o "$OUT/test_mcs_nl"
"$OUT/test_mcs_nl"

echo "ALL PASS"
