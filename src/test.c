/**
 * test.c - libwvw public API tests.
 * Summary: Tests each public libwvw function through one CTest case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "wvw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#define getpid _getpid
#else
#include <signal.h>
#include <unistd.h>
#endif

static int signal_count = 0;
static kc_wvw_t *signal_ctx_seen = NULL;

/**
 * Records one signal callback invocation.
 * @param ctx Context supplied by the library.
 * @return None.
 */
static void count_signal(kc_wvw_t *ctx) {
    if (ctx) { signal_count++; signal_ctx_seen = ctx; }
}

/**
 * Verifies one integer result.
 * @param name Check description.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("\033[31m[FAIL]\033[0m %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    printf("\033[32m[PASS]\033[0m %s\n", name);
    return 0;
}

/**
 * Verifies one boolean condition.
 * @param name Check description.
 * @param condition Non-zero when the check passed.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("\033[31m[FAIL]\033[0m %s\n", name);
        return 1;
    }
    printf("\033[32m[PASS]\033[0m %s\n", name);
    return 0;
}

/**
 * Tests kc_wvw_version.
 * @return 0 on success, 1 on failure.
 */
static int case_version(void) {
    kc_wvw_version();
    return expect_true("version does not crash", 1);
}

/**
 * Tests kc_wvw_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_options_default(void) {
    kc_wvw_options_default();
    return expect_true("options_default does not crash", 1);
}

/**
 * Tests kc_wvw_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_options_load_env(void) {
    kc_wvw_options_t opts = {0};
    kc_wvw_options_load_env(&opts);
    kc_wvw_options_load_env(NULL);
    return expect_true("load_env does not crash", 1);
}

/**
 * Tests kc_wvw_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_options_free(void) {
    kc_wvw_options_t opts = {0};
    kc_wvw_options_free(&opts);
    kc_wvw_options_free(NULL);
    return expect_true("options_free does not crash", 1);
}

/**
 * Tests kc_wvw_open error paths.
 * @return 0 on success, 1 on failure.
 */
static int case_open(void) {
    kc_wvw_t *ctx = NULL;
    kc_wvw_options_t opts;
    int rc;

    rc = 0;
    opts = kc_wvw_options_default();
    rc += expect_int("open(NULL, opts) returns ERROR", KC_WVW_ERROR, kc_wvw_open(NULL, &opts));
    rc += expect_int("open(out, NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_open(&ctx, NULL));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_close.
 * @return 0 on success, 1 on failure.
 */
static int case_close(void) {
    int rc;

    rc = 0;
    rc += expect_int("close(NULL) returns OK", KC_WVW_OK, kc_wvw_close(NULL));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_stop(void) {
    int rc;

    rc = 0;
    rc += expect_int("stop(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_stop(NULL));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_navigate.
 * @return 0 on success, 1 on failure.
 */
static int case_navigate(void) {
    int rc;

    rc = 0;
    rc += expect_int("navigate(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_navigate(NULL, "http://localhost"));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_enable_bridge.
 * @return 0 on success, 1 on failure.
 */
static int case_enable_bridge(void) {
    kc_wvw_bridge_options_t opts = {0};
    int rc;

    rc = 0;
    rc += expect_int("enable_bridge(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_enable_bridge(NULL, &opts));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_post_bridge_event.
 * @return 0 on success, 1 on failure.
 */
static int case_post_bridge_event(void) {
    int rc;

    rc = 0;
    rc += expect_int("post_bridge_event(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_post_bridge_event(NULL, "{}"));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_tray_init.
 * @return 0 on success, 1 on failure.
 */
static int case_tray_init(void) {
    int rc;

    rc = 0;
    rc += expect_int("tray_init(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_tray_init(NULL, "tip", NULL));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_tray_remove.
 * @return 0 on success, 1 on failure.
 */
static int case_tray_remove(void) {
    int rc;

    rc = 0;
    rc += expect_int("tray_remove(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_tray_remove(NULL));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_hide.
 * @return 0 on success, 1 on failure.
 */
static int case_hide(void) {
    int rc;

    rc = 0;
    rc += expect_int("hide(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_hide(NULL));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_show.
 * @return 0 on success, 1 on failure.
 */
static int case_show(void) {
    int rc;

    rc = 0;
    rc += expect_int("show(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_show(NULL));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_minimize.
 * @return 0 on success, 1 on failure.
 */
static int case_minimize(void) {
    int rc;

    rc = 0;
    rc += expect_int("minimize(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_minimize(NULL));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_tray_set_menu.
 * @return 0 on success, 1 on failure.
 */
static int case_tray_set_menu(void) {
    int rc;

    rc = 0;
    rc += expect_int("tray_set_menu(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_tray_set_menu(NULL, NULL, 0));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_on_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_on_signal(void) {
    int rc;

    rc = 0;
    signal_count = 0;
    rc += expect_int("on_signal NULL returns ERROR", KC_WVW_ERROR, kc_wvw_on_signal(NULL, 1, count_signal));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_raise_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_raise_signal(void) {
    int rc;

    rc = 0;
    signal_count = 0;
    signal_ctx_seen = NULL;
    rc += expect_int("raise_signal NULL returns ERROR", KC_WVW_ERROR, kc_wvw_raise_signal(NULL, 1));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_listen_signals.
 * @return 0 on success, 1 on failure.
 */
static int case_listen_signals(void) {
    int rc;

    rc = 0;
    rc += expect_int("listen_signals NULL returns ERROR", KC_WVW_ERROR, kc_wvw_listen_signals(NULL));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_listen_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_listen_signal(void) {
    int rc;

    rc = 0;
    rc += expect_int("listen_signal NULL returns ERROR", KC_WVW_ERROR, kc_wvw_listen_signal(NULL, 1));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_signal_listener.
 * @return 0 on success, 1 on failure.
 */
static int case_signal_listener(void) {
    kc_wvw_signal_listener(0);
    return expect_true("signal_listener does not crash", 1);
}

/**
 * Runs one libwvw public API test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 or 2 on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "version") == 0) return case_version();
    if (strcmp(argv[1], "options-default") == 0) return case_options_default();
    if (strcmp(argv[1], "options-load-env") == 0) return case_options_load_env();
    if (strcmp(argv[1], "options-free") == 0) return case_options_free();
    if (strcmp(argv[1], "open") == 0) return case_open();
    if (strcmp(argv[1], "close") == 0) return case_close();
    if (strcmp(argv[1], "stop") == 0) return case_stop();
    if (strcmp(argv[1], "navigate") == 0) return case_navigate();
    if (strcmp(argv[1], "enable-bridge") == 0) return case_enable_bridge();
    if (strcmp(argv[1], "post-bridge-event") == 0) return case_post_bridge_event();
    if (strcmp(argv[1], "tray-init") == 0) return case_tray_init();
    if (strcmp(argv[1], "tray-remove") == 0) return case_tray_remove();
    if (strcmp(argv[1], "hide") == 0) return case_hide();
    if (strcmp(argv[1], "show") == 0) return case_show();
    if (strcmp(argv[1], "minimize") == 0) return case_minimize();
    if (strcmp(argv[1], "tray-set-menu") == 0) return case_tray_set_menu();
    if (strcmp(argv[1], "on-signal") == 0) return case_on_signal();
    if (strcmp(argv[1], "raise-signal") == 0) return case_raise_signal();
    if (strcmp(argv[1], "listen-signals") == 0) return case_listen_signals();
    if (strcmp(argv[1], "listen-signal") == 0) return case_listen_signal();
    if (strcmp(argv[1], "signal-listener") == 0) return case_signal_listener();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
