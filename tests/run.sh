#!/bin/sh
# Build and run the host unit tests. Run from the repository root.
#
# LIBUBOX_DIR is the directory that contains the libubox sources as
# "libubox/" (default: the libubox submodule in this repository).
set -e

CC=${CC:-gcc}
CFLAGS="-std=c99 -D_GNU_SOURCE -Wall -Wextra -Werror -Isrc"
LIBUBOX_DIR=${LIBUBOX_DIR:-.}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

$CC $CFLAGS tests/test_mcs_table.c src/mcs_table.c -o "$OUT/test_mcs_table"
"$OUT/test_mcs_table"

$CC $CFLAGS tests/test_mcs_nl.c src/mcs_nl.c -o "$OUT/test_mcs_nl"
"$OUT/test_mcs_nl"

if [ ! -f "$LIBUBOX_DIR/libubox/uloop.c" ]; then
	echo "libubox sources not found, set LIBUBOX_DIR" >&2
	exit 1
fi

# The lifecycle test runs on the real event loop with a fake transport
$CC -std=gnu99 -D_GNU_SOURCE -w -I"$LIBUBOX_DIR" -c "$LIBUBOX_DIR/libubox/uloop.c" -o "$OUT/uloop.o"
$CC $CFLAGS -DMCS_REFRESH_MS=50 -I"$LIBUBOX_DIR" -Itests \
	tests/test_mcs.c tests/fake_mcs_nl.c src/mcs.c src/mcs_table.c "$OUT/uloop.o" \
	-o "$OUT/test_mcs"
"$OUT/test_mcs"

echo "ALL PASS"
