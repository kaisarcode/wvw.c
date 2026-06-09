#!/bin/sh
# test.sh
# Summary: Validation suite for wvw functionality.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

# Prints one failure line.
# @param $1 Failure message.
# @return 1 on failure.
kc_test_fail() {
    printf '\033[31m[FAIL]\033[0m %s\n' "$1"
    return 1
}

# Prints one success line.
# @param $1 Success message.
# @return 0 on success.
kc_test_pass() {
    printf '\033[32m[PASS]\033[0m %s\n' "$1"
}

# Detects the artifact architecture for the current machine.
# @return Architecture name on stdout.
kc_test_arch() {
    case "$(uname -m)" in
        x86_64 | amd64)
            printf '%s\n' "x86_64"
            ;;
        *)
            uname -m
            ;;
    esac
}

# Detects the artifact platform for the current machine.
# @return Platform name on stdout.
kc_test_platform() {
    case "$(uname -s)" in
        Linux)
            printf '%s\n' "linux"
            ;;
        *)
            uname -s | tr '[:upper:]' '[:lower:]'
            ;;
    esac
}

# Returns the CLI path for the current architecture and platform.
# @return CLI path on stdout.
kc_test_binary_path() {
    printf './bin/%s/%s/wvw\n' "$(kc_test_arch)" "$(kc_test_platform)"
}

# Verifies the binary exists and is executable.
# @return 0 on success or 1 on failure.
kc_test_check_binary() {
    if [ ! -x "$BIN" ]; then
        kc_test_fail "binary not found: $BIN"
        return 1
    fi

    return 0
}

# Tests that the binary prints help successfully.
# @return 0 on success or 1 on failure.
kc_test_help() {
    if ! "$BIN" --help > /dev/null 2>&1; then
        kc_test_fail "help output"
        return 1
    fi

    kc_test_pass "help output"
}

# Tests that the binary prints version successfully.
# @return 0 on success or 1 on failure.
kc_test_version() {
    if ! "$BIN" --version > /dev/null 2>&1; then
        kc_test_fail "version output"
        return 1
    fi

    kc_test_pass "version output"
}

# Tests that the binary rejects missing URLs.
# @return 0 on success or 1 on failure.
kc_test_missing_url() {
    if "$BIN" --title "test" > /dev/null 2>&1; then
        kc_test_fail "missing URL validation"
        return 1
    fi

    kc_test_pass "missing URL validation"
}

# Runs the full validation suite.
# @return 0 on success or 1 on failure.
kc_test_main() {
    failed=0

    BIN=$(kc_test_binary_path)

    kc_test_check_binary || exit 1

    kc_test_help        || failed=$((failed + 1))
    kc_test_version     || failed=$((failed + 1))
    kc_test_missing_url || failed=$((failed + 1))

    return $failed
}

kc_test_main
