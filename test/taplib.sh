#!/bin/bash

TEST_COUNT=$(cat $0 | grep -v "TEST_COUNT" | \
             grep -oP "assert_(ok|skip|fail)[ \t]*(?=[^(])"\ | wc -l)

TESTS_PERFORMED=0

echo "1..$TEST_COUNT"

assert_ok() {
    local label="$1"
    __assert "$label"
}

assert_skip() {
    local label="$1"
    echo "true" | __assert "$label" "SKIP"
    TESTS_PERFORMED=$((TESTS_PERFORMED + 1))
}

assert_fail() {
    local label="$1"
    __assert "$label" "TODO [Expected failure]"
    test $? != 0
}

abort_tests() {
    local message="$1"
    echo "Bail out! $message"
    exit 255
}

#------------------------------------------------------------------------------#

__assert() {
    local label="$1"
    local directive="$2"
    local result=255

    TESTS_PERFORMED=$((TESTS_PERFORMED + 1))

    if [ "x$directive" == "x" ]; then
        directive=""
    else
        directive=" # $directive"
    fi

    if [ "x$label" == "x" ]; then
        echo "not ok: error: label not set for test" >&2
        return 1
    fi

    set -o pipefail
    source /dev/stdin | sed 's/^/# /g'
    result=$?

    if [ "$result" != "0" ]; then
        echo "not ok: $label$directive"
        return $result
    fi

    echo "ok: $label$directive"
}
