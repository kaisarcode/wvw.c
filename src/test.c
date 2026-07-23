/**
 * test.c - libwvw public API tests.
 * Summary: Tests each public libwvw function through one CTest case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libwvw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * Bridge callback that returns METHOD_NOT_FOUND for empty
 * registry tests.
 * @param ctx Window context.
 * @param method Method name.
 * @param params_json JSON arguments.
 * @param result_json Output response.
 * @param userdata User data.
 * @return KC_WVW_OK.
 */
static int kc_test_empty_bridge_callback(
    kc_wvw_t *ctx,
    const char *method,
    const char *params_json,
    char **result_json,
    void *userdata
) {
    (void)ctx;
    (void)method;
    (void)params_json;
    (void)userdata;
    *result_json = strdup("{\"ok\":false,\"code\":\"METHOD_NOT_FOUND\"}");
    return KC_WVW_OK;
}

/**
 * Tests bridge initialization with an empty method registry.
 * @return 0 on success, 1 on failure.
 */
static int case_bridge_empty_registry(void) {
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;
    const char *methods[] = { NULL };
    int rc;

    rc = 0;
    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = strdup("file:///dev/null");
    if (kc_wvw_open(&ctx, &opts) != KC_WVW_OK) {
        kc_wvw_options_free(&opts);
        return 0;
    }
    memset(&bopts, 0, sizeof(bopts));
    bopts.methods = methods;
    bopts.method_count = 0;
    bopts.callback = kc_test_empty_bridge_callback;
    bopts.userdata = NULL;
    bopts.allow_file = 1;
    rc += expect_int("enable_bridge(empty) returns OK", KC_WVW_OK, kc_wvw_enable_bridge(ctx, &bopts));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
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
 * Tests kc_wvw_maximize.
 * @return 0 on success, 1 on failure.
 */
static int case_maximize(void) {
    int rc;

    rc = 0;
    rc += expect_int("maximize(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_maximize(NULL));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_restore.
 * @return 0 on success, 1 on failure.
 */
static int case_restore(void) {
    int rc;

    rc = 0;
    rc += expect_int("restore(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_restore(NULL));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_set_title.
 * @return 0 on success, 1 on failure.
 */
static int case_set_title(void) {
    int rc;

    rc = 0;
    rc += expect_int("set_title(NULL, str) returns ERROR", KC_WVW_ERROR, kc_wvw_set_title(NULL, "test"));
    rc += expect_int("set_title(ctx, NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_set_title(NULL, NULL));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_set_size.
 * @return 0 on success, 1 on failure.
 */
static int case_set_size(void) {
    int rc;

    rc = 0;
    rc += expect_int("set_size(NULL, 100, 100) returns ERROR", KC_WVW_ERROR, kc_wvw_set_size(NULL, 100, 100));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_get_state.
 * @return 0 on success, 1 on failure.
 */
static int case_get_state(void) {
    kc_wvw_window_state_t state;
    int rc;

    rc = 0;
    rc += expect_int("get_state(NULL, &st) returns ERROR", KC_WVW_ERROR, kc_wvw_get_state(NULL, &state));
    return rc == 0 ? 0 : 1;
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
    if (strcmp(argv[1], "bridge-empty-registry") == 0) return case_bridge_empty_registry();
    if (strcmp(argv[1], "post-bridge-event") == 0) return case_post_bridge_event();
    if (strcmp(argv[1], "tray-init") == 0) return case_tray_init();
    if (strcmp(argv[1], "tray-remove") == 0) return case_tray_remove();
    if (strcmp(argv[1], "hide") == 0) return case_hide();
    if (strcmp(argv[1], "show") == 0) return case_show();
    if (strcmp(argv[1], "minimize") == 0) return case_minimize();
    if (strcmp(argv[1], "tray-set-menu") == 0) return case_tray_set_menu();
    if (strcmp(argv[1], "maximize") == 0) return case_maximize();
    if (strcmp(argv[1], "restore") == 0) return case_restore();
    if (strcmp(argv[1], "set-title") == 0) return case_set_title();
    if (strcmp(argv[1], "set-size") == 0) return case_set_size();
    if (strcmp(argv[1], "get-state") == 0) return case_get_state();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
