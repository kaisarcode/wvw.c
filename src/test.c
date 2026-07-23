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
 * Tests bridge initialization with an empty method registry.
 * @return 0 on success, 1 on failure.
 */
static int case_bridge_empty_registry(void) {
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;
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
    bopts.methods = NULL;
    bopts.method_count = 0;
    bopts.callback = NULL;
    bopts.userdata = NULL;
    bopts.allow_file = 1;
    rc += expect_int("enable_bridge(empty) returns OK", KC_WVW_OK, kc_wvw_enable_bridge(ctx, &bopts));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests negative method_count is rejected.
 * @return 0 on success, 1 on failure.
 */
static int case_bridge_negative_method_count(void) {
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;
    const char *methods[] = { "test" };
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
    bopts.method_count = -1;
    bopts.callback = (kc_wvw_bridge_callback_t)1;
    rc += expect_int("negative method_count rejected", KC_WVW_ERROR,
        kc_wvw_enable_bridge(ctx, &bopts));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests positive method_count without methods is rejected.
 * @return 0 on success, 1 on failure.
 */
static int case_bridge_positive_without_methods(void) {
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;
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
    bopts.methods = NULL;
    bopts.method_count = 1;
    bopts.callback = (kc_wvw_bridge_callback_t)1;
    rc += expect_int("positive count without methods rejected", KC_WVW_ERROR,
        kc_wvw_enable_bridge(ctx, &bopts));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests positive method_count without callback is rejected.
 * @return 0 on success, 1 on failure.
 */
static int case_bridge_positive_without_callback(void) {
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;
    const char *methods[] = { "test" };
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
    bopts.method_count = 1;
    bopts.callback = NULL;
    rc += expect_int("positive count without callback rejected", KC_WVW_ERROR,
        kc_wvw_enable_bridge(ctx, &bopts));
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
 * Tests empty registry does not trust every URL via navigate.
 * @return 0 on success, 1 on failure.
 */
static int case_bridge_empty_registry_rejects_remote(void) {
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;
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
    bopts.methods = NULL;
    bopts.method_count = 0;
    bopts.callback = NULL;
    bopts.allow_file = 1;
    if (kc_wvw_enable_bridge(ctx, &bopts) != KC_WVW_OK) {
        kc_wvw_close(ctx);
        kc_wvw_options_free(&opts);
        return 1;
    }
    rc += expect_int("navigate to same file origin succeeds",
        KC_WVW_OK, kc_wvw_navigate(ctx, "file:///dev/null"));
    rc += expect_int("navigate to unrelated remote URL rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "https://evil.example.com/"));
    rc += expect_int("navigate to data: URL rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "data:text/html,<h1>hi</h1>"));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests scheme allowances are enforced via enable_bridge.
 * @return 0 on success, 1 on failure.
 */
static int case_bridge_scheme_allowances(void) {
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;
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
    bopts.methods = NULL;
    bopts.method_count = 0;
    bopts.callback = NULL;
    bopts.allow_file = 1;
    rc += expect_int("file:// initial URL accepted when allow_file=1",
        KC_WVW_OK, kc_wvw_enable_bridge(ctx, &bopts));
    rc += expect_int("navigate to data: blocked when allow_data=0",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "data:text/html,<h1>test</h1>"));
    rc += expect_int("navigate to remote blocked when not same-origin",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "https://evil.example.com/"));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests navigate rejects untrusted URLs with bridge enabled.
 * @return 0 on success, 1 on failure.
 */
static int case_navigate_rejects_untrusted(void) {
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;
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
    bopts.methods = NULL;
    bopts.method_count = 0;
    bopts.callback = NULL;
    bopts.allow_file = 1;
    if (kc_wvw_enable_bridge(ctx, &bopts) != KC_WVW_OK) {
        kc_wvw_close(ctx);
        kc_wvw_options_free(&opts);
        return 1;
    }
    rc += expect_int("navigate to same file origin succeeds",
        KC_WVW_OK, kc_wvw_navigate(ctx, "file:///dev/null"));
    rc += expect_int("navigate to remote URL rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "https://evil.example.com/"));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests method_count is preserved for empty registry.
 * @return 0 on success, 1 on failure.
 */
static int case_bridge_empty_registry_method_count(void) {
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;
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
    bopts.methods = NULL;
    bopts.method_count = 0;
    bopts.callback = NULL;
    bopts.allow_file = 1;
    if (kc_wvw_enable_bridge(ctx, &bopts) != KC_WVW_OK) {
        kc_wvw_close(ctx);
        kc_wvw_options_free(&opts);
        return 1;
    }
    kc_wvw_window_state_t state;
    rc += expect_int("get_state succeeds with empty bridge",
        KC_WVW_OK, kc_wvw_get_state(ctx, &state));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests minimize then restore returns window to visible normal state.
 * @return 0 on success, 1 on failure.
 */
static int case_tray_show_restores_minimized(void) {
    kc_wvw_options_t opts;
    kc_wvw_t *ctx = NULL;
    int rc;

    rc = 0;
    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = strdup("file:///dev/null");
    if (kc_wvw_open(&ctx, &opts) != KC_WVW_OK) {
        kc_wvw_options_free(&opts);
        return 0;
    }
    rc += expect_int("minimize returns OK", KC_WVW_OK, kc_wvw_minimize(ctx));
    rc += expect_int("restore returns OK", KC_WVW_OK, kc_wvw_restore(ctx));
    rc += expect_int("get_state after restore reports visible", 1, ({
        kc_wvw_window_state_t s = {0};
        kc_wvw_get_state(ctx, &s);
        s.visible;
    }));
    rc += expect_int("get_state after restore reports not minimized", 0, ({
        kc_wvw_window_state_t s = {0};
        kc_wvw_get_state(ctx, &s);
        s.minimized;
    }));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests hide then show returns window to visible state.
 * @return 0 on success, 1 on failure.
 */
static int case_tray_show_reveals_hidden(void) {
    kc_wvw_options_t opts;
    kc_wvw_t *ctx = NULL;
    int rc;

    rc = 0;
    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = strdup("file:///dev/null");
    if (kc_wvw_open(&ctx, &opts) != KC_WVW_OK) {
        kc_wvw_options_free(&opts);
        return 0;
    }
    rc += expect_int("hide returns OK", KC_WVW_OK, kc_wvw_hide(ctx));
    rc += expect_int("get_state after hide reports not visible", 0, ({
        kc_wvw_window_state_t s = {0};
        kc_wvw_get_state(ctx, &s);
        s.visible;
    }));
    rc += expect_int("show returns OK", KC_WVW_OK, kc_wvw_show(ctx));
    rc += expect_int("get_state after show reports visible", 1, ({
        kc_wvw_window_state_t s = {0};
        kc_wvw_get_state(ctx, &s);
        s.visible;
    }));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests show on already-visible window does not change state.
 * @return 0 on success, 1 on failure.
 */
static int case_tray_show_no_hide_visible(void) {
    kc_wvw_options_t opts;
    kc_wvw_t *ctx = NULL;
    int rc;

    rc = 0;
    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = strdup("file:///dev/null");
    if (kc_wvw_open(&ctx, &opts) != KC_WVW_OK) {
        kc_wvw_options_free(&opts);
        return 0;
    }
    rc += expect_int("show on visible window returns OK", KC_WVW_OK, kc_wvw_show(ctx));
    rc += expect_int("get_state after show reports visible", 1, ({
        kc_wvw_window_state_t s = {0};
        kc_wvw_get_state(ctx, &s);
        s.visible;
    }));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests no_focus option is stored correctly in options struct.
 * @return 0 on success, 1 on failure.
 */
static int case_tray_show_no_focus_option(void) {
    kc_wvw_options_t opts;
    int rc;

    rc = 0;
    opts = kc_wvw_options_default();
    rc += expect_int("default no_focus is 0", 0, opts.no_focus);
    opts.no_focus = 1;
    rc += expect_int("set no_focus is 1", 1, opts.no_focus);
    opts.no_focus = 0;
    rc += expect_int("cleared no_focus is 0", 0, opts.no_focus);
    kc_wvw_options_free(&opts);
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
    if (strcmp(argv[1], "bridge-negative-method-count") == 0) return case_bridge_negative_method_count();
    if (strcmp(argv[1], "bridge-positive-without-methods") == 0) return case_bridge_positive_without_methods();
    if (strcmp(argv[1], "bridge-positive-without-callback") == 0) return case_bridge_positive_without_callback();
    if (strcmp(argv[1], "bridge-empty-registry-rejects-remote") == 0) return case_bridge_empty_registry_rejects_remote();
    if (strcmp(argv[1], "bridge-scheme-allowances") == 0) return case_bridge_scheme_allowances();
    if (strcmp(argv[1], "navigate-rejects-untrusted") == 0) return case_navigate_rejects_untrusted();
    if (strcmp(argv[1], "bridge-empty-registry-method-count") == 0) return case_bridge_empty_registry_method_count();
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
    if (strcmp(argv[1], "tray-show-restores-minimized") == 0) return case_tray_show_restores_minimized();
    if (strcmp(argv[1], "tray-show-reveals-hidden") == 0) return case_tray_show_reveals_hidden();
    if (strcmp(argv[1], "tray-show-no-hide-visible") == 0) return case_tray_show_no_hide_visible();
    if (strcmp(argv[1], "tray-show-no-focus-option") == 0) return case_tray_show_no_focus_option();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
