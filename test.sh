#!/bin/sh
# Summary: Validation suite for wvw CLI parsing and library API behavior.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

PASS=0
FAIL=0
TMP_ROOT=

# Prints one failure line.
# @param $1 Failure message.
# @return 1 on failure.
kc_test_fail() {
    FAIL=$((FAIL + 1))
    printf '\033[31m[FAIL]\033[0m %s\n' "$1"
    return 1
}

# Prints one success line.
# @param $1 Success message.
# @return 0 on success.
kc_test_pass() {
    PASS=$((PASS + 1))
    printf '\033[32m[PASS]\033[0m %s\n' "$1"
    return 0
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

# Returns the shared library path for the current architecture and platform.
# @return Shared library path on stdout.
kc_test_shared_library_path() {
    printf './bin/%s/%s/libwvw.so\n' "$(kc_test_arch)" "$(kc_test_platform)"
}

# Returns the static library path for the current architecture and platform.
# @return Static library path on stdout.
kc_test_static_library_path() {
    printf './bin/%s/%s/libwvw.a\n' "$(kc_test_arch)" "$(kc_test_platform)"
}

# Verifies the CLI and library artifacts exist.
# @return 0 on success or 1 on failure.
kc_test_check_artifacts() {
    if [ ! -x "$BIN" ]; then
        kc_test_fail "binary path: expected executable at $BIN, got missing"
        return 1
    fi
    if [ ! -f "$SHARED_LIB" ]; then
        kc_test_fail "shared library path: expected file at $SHARED_LIB, got missing"
        return 1
    fi
    if [ ! -f "$STATIC_LIB" ]; then
        kc_test_fail "static library path: expected file at $STATIC_LIB, got missing"
        return 1
    fi
    kc_test_pass "artifacts present: binary and shared/static libraries found"
    return 0
}

# Verifies the CLI rejects a missing URL.
# @return 0 on success or 1 on failure.
kc_test_cli_missing_url() {
    if "$BIN" --title test > /dev/null 2>&1; then
        kc_test_fail "missing URL: expected non-zero exit, got 0"
        return 1
    fi
    kc_test_pass "missing URL: expected non-zero exit, got non-zero"
    return 0
}

# Verifies the CLI rejects a missing --url value.
# @return 0 on success or 1 on failure.
kc_test_cli_missing_url_value() {
    if "$BIN" --url > /dev/null 2>&1; then
        kc_test_fail "--url without value: expected non-zero exit, got 0"
        return 1
    fi
    kc_test_pass "--url without value: expected non-zero exit, got non-zero"
    return 0
}

# Verifies the CLI rejects a missing --title value.
# @return 0 on success or 1 on failure.
kc_test_cli_missing_title_value() {
    if "$BIN" --url https://example.com --title > /dev/null 2>&1; then
        kc_test_fail "--title without value: expected non-zero exit, got 0"
        return 1
    fi
    kc_test_pass "--title without value: expected non-zero exit, got non-zero"
    return 0
}

# Verifies the CLI rejects an invalid width value.
# @return 0 on success or 1 on failure.
kc_test_cli_invalid_width() {
    if "$BIN" --url https://example.com --width wide > /dev/null 2>&1; then
        kc_test_fail "invalid width input 'wide': expected non-zero exit, got 0"
        return 1
    fi
    kc_test_pass "invalid width input 'wide': expected non-zero exit, got non-zero"
    return 0
}

# Verifies the CLI rejects an invalid height value.
# @return 0 on success or 1 on failure.
kc_test_cli_invalid_height() {
    if "$BIN" --url https://example.com --height tall > /dev/null 2>&1; then
        kc_test_fail "invalid height input 'tall': expected non-zero exit, got 0"
        return 1
    fi
    kc_test_pass "invalid height input 'tall': expected non-zero exit, got non-zero"
    return 0
}

# Verifies the CLI rejects unknown options.
# @return 0 on success or 1 on failure.
kc_test_cli_unknown_option() {
    if "$BIN" --url https://example.com --bogus > /dev/null 2>&1; then
        kc_test_fail "unknown option '--bogus': expected non-zero exit, got 0"
        return 1
    fi
    kc_test_pass "unknown option '--bogus': expected non-zero exit, got non-zero"
    return 0
}

# Writes the helper source that validates the public library API.
# @param $1 Destination source file path.
# @return 0 on success.
kc_test_write_helper() {
    printf '%s\n' \
        '#include "wvw.h"' \
        '#include <signal.h>' \
        '#include <stdlib.h>' \
        '#include <string.h>' \
        '' \
        'static int bridge_cb(' \
        'kc_wvw_t *ctx,' \
        'const char *method,' \
        'const char *params_json,' \
        'char **result_json,' \
        'void *userdata' \
        ') {' \
        '    (void)ctx;' \
        '    (void)method;' \
        '    (void)params_json;' \
        '    (void)result_json;' \
        '    (void)userdata;' \
        '    return KC_WVW_OK;' \
        '}' \
        '' \
        'int main(void) {' \
        '    kc_wvw_t *ctx = NULL;' \
        '    kc_wvw_options_t opts = kc_wvw_options_default();' \
        '    const char *methods[] = { "Ping" };' \
        '    kc_wvw_bridge_options_t bridge;' \
        '' \
        '    if (opts.url != NULL || opts.title != NULL) return 1;' \
        '    if (opts.width != 1280 || opts.height != 720) return 1;' \
        '    if (opts.fullscreen != 0 || opts.borderless != 0) return 1;' \
        '    if (opts.always_on_top != 0 || opts.click_through != 0 || opts.no_focus != 0) return 1;' \
        '' \
        '    setenv("KC_WVW_URL", "https://example.com", 1);' \
        '    setenv("KC_WVW_TITLE", "Example", 1);' \
        '    setenv("KC_WVW_WIDTH", "1440", 1);' \
        '    setenv("KC_WVW_HEIGHT", "900", 1);' \
        '    setenv("KC_WVW_FULLSCREEN", "1", 1);' \
        '    setenv("KC_WVW_BORDERLESS", "1", 1);' \
        '    setenv("KC_WVW_ALWAYS_ON_TOP", "1", 1);' \
        '    setenv("KC_WVW_CLICK_THROUGH", "1", 1);' \
        '    setenv("KC_WVW_NO_FOCUS", "1", 1);' \
        '    kc_wvw_options_load_env(&opts);' \
        '    if (!opts.url || strcmp(opts.url, "https://example.com") != 0) return 1;' \
        '    if (!opts.title || strcmp(opts.title, "Example") != 0) return 1;' \
        '    if (opts.width != 1440 || opts.height != 900) return 1;' \
        '    if (opts.fullscreen != 1 || opts.borderless != 1) return 1;' \
        '    if (opts.always_on_top != 1 || opts.click_through != 1 || opts.no_focus != 1) return 1;' \
        '' \
        '    setenv("KC_WVW_WIDTH", "bad", 1);' \
        '    setenv("KC_WVW_HEIGHT", "nope", 1);' \
        '    kc_wvw_options_load_env(&opts);' \
        '    if (opts.width != 1440 || opts.height != 900) return 1;' \
        '' \
        '    kc_wvw_options_free(&opts);' \
        '    if (opts.url != NULL || opts.title != NULL) return 1;' \
        '' \
        '    unsetenv("KC_WVW_URL");' \
        '    unsetenv("KC_WVW_TITLE");' \
        '    unsetenv("KC_WVW_WIDTH");' \
        '    unsetenv("KC_WVW_HEIGHT");' \
        '    unsetenv("KC_WVW_FULLSCREEN");' \
        '    unsetenv("KC_WVW_BORDERLESS");' \
        '    unsetenv("KC_WVW_ALWAYS_ON_TOP");' \
        '    unsetenv("KC_WVW_CLICK_THROUGH");' \
        '    unsetenv("KC_WVW_NO_FOCUS");' \
        '' \
        '    if (kc_wvw_open(NULL, &opts) != KC_WVW_ERROR) return 1;' \
        '    if (kc_wvw_open(&ctx, NULL) != KC_WVW_ERROR) return 1;' \
        '    if (kc_wvw_open(&ctx, &opts) != KC_WVW_ERROR) return 1;' \
        '    if (kc_wvw_close(NULL) != KC_WVW_OK) return 1;' \
        '    if (kc_wvw_run(NULL) != KC_WVW_ERROR) return 1;' \
        '    if (kc_wvw_navigate(NULL, "https://example.com") != KC_WVW_ERROR) return 1;' \
        '    if (kc_wvw_on_signal(NULL, SIGINT, NULL) != KC_WVW_ERROR) return 1;' \
        '    if (kc_wvw_raise_signal(NULL, SIGINT) != KC_WVW_ERROR) return 1;' \
        '    if (kc_wvw_listen_signals(NULL) != KC_WVW_ERROR) return 1;' \
        '    if (kc_wvw_listen_signal(NULL, SIGINT) != KC_WVW_ERROR) return 1;' \
        '' \
        '    memset(&bridge, 0, sizeof(bridge));' \
        '    bridge.methods = methods;' \
        '    bridge.method_count = 1;' \
        '    bridge.callback = bridge_cb;' \
        '    bridge.allow_file = 1;' \
        '    if (kc_wvw_enable_bridge(NULL, &bridge) != KC_WVW_ERROR) return 1;' \
        '    if (kc_wvw_enable_bridge(ctx, NULL) != KC_WVW_ERROR) return 1;' \
        '    if (kc_wvw_post_bridge_event(NULL, "{}") != KC_WVW_ERROR) return 1;' \
        '' \
        '    return 0;' \
        '}' > "$1"
    return 0
}

# Compiles and runs one helper against the built shared library.
# @return 0 on success or 1 on failure.
kc_test_library_api() {
    helper_c="$TMP_ROOT/wvw-api-test.c"
    helper_bin="$TMP_ROOT/wvw-api-test"
    lib_dir=$(dirname "$SHARED_LIB")

    kc_test_write_helper "$helper_c" || return 1
    if ! cc -I./src "$helper_c" -L"$lib_dir" -Wl,-rpath,"$lib_dir" -lwvw -o "$helper_bin"; then
        kc_test_fail "library API helper compile: expected success, got compiler failure"
        return 1
    fi
    if ! LD_LIBRARY_PATH="$lib_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$helper_bin"; then
        kc_test_fail "library API contract helper: expected exit 0, got non-zero"
        return 1
    fi
    kc_test_pass "library API contracts: defaults, env loading, null guards, and signal hooks matched expectations"
    return 0
}

# Runs the full validation suite.
# @return 0 on success or 1 on failure.
kc_test_main() {
    failed=0

    TMP_ROOT=$(mktemp -d)
    BIN=$(kc_test_binary_path)
    SHARED_LIB=$(kc_test_shared_library_path)
    STATIC_LIB=$(kc_test_static_library_path)

    kc_test_check_artifacts || return 1
    kc_test_cli_missing_url || failed=$((failed + 1))
    kc_test_cli_missing_url_value || failed=$((failed + 1))
    kc_test_cli_missing_title_value || failed=$((failed + 1))
    kc_test_cli_invalid_width || failed=$((failed + 1))
    kc_test_cli_invalid_height || failed=$((failed + 1))
    kc_test_cli_unknown_option || failed=$((failed + 1))
    kc_test_library_api || failed=$((failed + 1))

    if [ "$failed" -ne 0 ]; then
        return 1
    fi
    return 0
}

trap 'if [ -n "$TMP_ROOT" ] && [ -d "$TMP_ROOT" ]; then rm -rf "$TMP_ROOT"; fi' EXIT HUP INT TERM

if kc_test_main; then
    exit 0
fi
exit 1
