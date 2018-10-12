#!/bin/sh

WORKDIR=`mktemp -d -p.`

if [ "x$CC" = "x" ]; then
    CC="gcc"
fi

if [ "x$BLACKLIST" = "x" ]; then
    BLACKLIST=""
fi

trap 'rm -rf $WORKDIR' INT TERM
set -eu
mkdir -p "$WORKDIR"

$CC -Wall -Wextra -pedantic -Q --help=warning | \
    grep -P "(disabled|[=] )" | \
    grep -Po "[-]W[^ \t=]+" | sort | uniq > "$WORKDIR/lint_flags.mk.temp.init"

echo "int main(void) { return 0;}" > "$WORKDIR/lint_flags.mk.temp.c"

$CC $(cat "$WORKDIR/lint_flags.mk.temp.init") \
    "$WORKDIR/lint_flags.mk.temp.c" -o /dev/null 2>&1 | \
    grep "error: " | grep -oP "[-]W[a-zA-Z0-9_-]+" | \
    sort | uniq > "$WORKDIR/lint_flags.mk.temp.blacklist"

grep -vFf "$WORKDIR/lint_flags.mk.temp.blacklist" \
    "$WORKDIR/lint_flags.mk.temp.init" \
    > "$WORKDIR/lint_flags.mk.temp.works"

$CC $(cat "$WORKDIR/lint_flags.mk.temp.works") \
    "$WORKDIR/lint_flags.mk.temp.c" -o /dev/null 2>&1 | \
    grep -P "is valid for [^ ]+ but not for C" | \
    grep -oP "[-]W[a-zA-Z0-9_+-]+" > "$WORKDIR/lint_flags.mk.temp.blacklist"

grep -vFf "$WORKDIR/lint_flags.mk.temp.blacklist" \
    "$WORKDIR/lint_flags.mk.temp.works" \
    > "$WORKDIR/lint_flags.mk.temp.ok"

echo "$BLACKLIST" | tr ' ' '\n' | sort | uniq | sed -r 's/[ \t]//g' | \
    sed '/^$/d' > "$WORKDIR/blacklist"

grep -vFf "$WORKDIR/blacklist" "$WORKDIR/lint_flags.mk.temp.ok"
rm -rf "$WORKDIR"
