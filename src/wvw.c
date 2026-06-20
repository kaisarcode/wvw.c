/**
 * wvw.c - Native WebView window runner.
 * Summary: Command line interface for opening one native WebView window.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "wvw.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Print command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_print_help(const char *name) {
    printf("Usage: %s [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("    --url <url>       Set initial URL\n");
    printf("    --title <title>   Set window title\n");
    printf("    --background <hex> Set WebView background color as RRGGBB or AARRGGBB\n");
    printf("    --width <px>      Set window width\n");
    printf("    --height <px>     Set window height\n");
    printf("    --fullscreen      Start in fullscreen mode\n");
    printf("    --borderless      Start in borderless mode\n");
    printf("    --always-on-top   Keep the window above normal windows\n");
    printf("    --click-through   Ignore mouse input on the host window\n");
    printf("    --no-focus        Do not activate the window for keyboard focus\n");
    printf("    --bridge          Enable NativeBridge with built-in demo methods\n");
    printf("    -h, --help        Show this help\n");
    printf("    -v, --version     Show build version\n");
}

/**
 * Close the active window when one operating-system signal arrives.
 * @param ctx Window context.
 * @return None.
 */
static void kc_wvw_signal_cb(kc_wvw_t *ctx) {
    kc_wvw_close(ctx);
}

/**
 * Parse one integer option value.
 * @param text Decimal text.
 * @param out_value Destination integer pointer.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_parse_int(const char *text, int *out_value) {
    char *end;
    long value;

    if (!text || !out_value) {
        return KC_WVW_ERROR;
    }

    value = strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        return KC_WVW_ERROR;
    }

    *out_value = (int)value;
    return KC_WVW_OK;
}

/**
 * Handle NativeBridge method calls from the WebView.
 * @param ctx Window context.
 * @param method Method name called from JavaScript.
 * @param params_json JSON arguments from JavaScript.
 * @param result_json Output pointer for response JSON.
 * @param userdata User data pointer.
 * @return KC_WVW_OK after writing the result.
 */
static int kc_cli_bridge_callback(
    kc_wvw_t *ctx,
    const char *method,
    const char *params_json,
    char **result_json,
    void *userdata
) {
    (void)ctx;
    (void)userdata;
    char *buf;
    int n;

    if (strcmp(method, "ping") == 0) {
        *result_json = strdup("{\"ok\":true,\"pong\":true}");
        return KC_WVW_OK;
    }

    if (strcmp(method, "echo") == 0) {
        n = snprintf(NULL, 0, "{\"ok\":true,\"echo\":%s}",
            params_json ? params_json : "null");
        buf = malloc(n + 1);
        if (buf) {
            snprintf(buf, n + 1, "{\"ok\":true,\"echo\":%s}",
                params_json ? params_json : "null");
        }
        *result_json = buf;
        return KC_WVW_OK;
    }

    if (strcmp(method, "getStats") == 0) {
        *result_json = strdup(
            "{\"ok\":true,\"cpu\":\"23%\",\"mem\":\"6.2/15.6 GB\","
            "\"bat\":\"78%\",\"net\":\"\xe2\xac\x87 1.2 \xe2\xac\x86 0.4 MB/s\"}"
        );
        return KC_WVW_OK;
    }

    if (strcmp(method, "notifyHost") == 0) {
        fprintf(stderr, "[wvw bridge] host notification: %s\n",
            params_json ? params_json : "");
        *result_json = strdup("{\"ok\":true,\"received\":true}");
        return KC_WVW_OK;
    }

    n = snprintf(NULL, 0,
        "{\"ok\":false,\"error\":\"unknown method '%s'\"}", method);
    buf = malloc(n + 1);
    if (buf) {
        snprintf(buf, n + 1,
            "{\"ok\":false,\"error\":\"unknown method '%s'\"}", method);
    }
    *result_json = buf;
    return KC_WVW_OK;
}

/**
 * Execute the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_wvw_options_t opts = kc_wvw_options_default();
    kc_wvw_t *ctx = NULL;
    int i = 1;
    int bridge_enabled = 0;

    kc_wvw_options_load_env(&opts);

    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_print_help(argv[0]);
            kc_wvw_options_free(&opts);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("wvw build %llu\n",
                (unsigned long long)kc_wvw_version());
            kc_wvw_options_free(&opts);
            return 0;
        } else if (strcmp(argv[i], "--url") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "wvw: missing value for --url\n");
                kc_wvw_options_free(&opts);
                return 1;
            }
            free(opts.url);
            opts.url = strdup(argv[i]);
        } else if (strcmp(argv[i], "--title") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "wvw: missing value for --title\n");
                kc_wvw_options_free(&opts);
                return 1;
            }
            free(opts.title);
            opts.title = strdup(argv[i]);
        } else if (strcmp(argv[i], "--background") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "wvw: missing value for --background\n");
                kc_wvw_options_free(&opts);
                return 1;
            }
            free(opts.background);
            opts.background = strdup(argv[i]);
        } else if (strcmp(argv[i], "--width") == 0) {
            if (++i >= argc || kc_wvw_parse_int(argv[i], &opts.width) != KC_WVW_OK) {
                fprintf(stderr, "wvw: invalid value for --width\n");
                kc_wvw_options_free(&opts);
                return 1;
            }
        } else if (strcmp(argv[i], "--height") == 0) {
            if (++i >= argc || kc_wvw_parse_int(argv[i], &opts.height) != KC_WVW_OK) {
                fprintf(stderr, "wvw: invalid value for --height\n");
                kc_wvw_options_free(&opts);
                return 1;
            }
        } else if (strcmp(argv[i], "--fullscreen") == 0) {
            opts.fullscreen = 1;
        } else if (strcmp(argv[i], "--borderless") == 0) {
            opts.borderless = 1;
        } else if (strcmp(argv[i], "--always-on-top") == 0) {
            opts.always_on_top = 1;
        } else if (strcmp(argv[i], "--click-through") == 0) {
            opts.click_through = 1;
        } else if (strcmp(argv[i], "--no-focus") == 0) {
            opts.no_focus = 1;
        } else if (strcmp(argv[i], "--bridge") == 0) {
            bridge_enabled = 1;
        } else {
            fprintf(stderr, "wvw: unknown option '%s'\n", argv[i]);
            kc_wvw_options_free(&opts);
            return 1;
        }
        i++;
    }

    if (!opts.url || !*opts.url) {
        fprintf(stderr, "wvw: missing URL\n");
        kc_wvw_options_free(&opts);
        return 1;
    }

    if (kc_wvw_open(&ctx, &opts) != KC_WVW_OK) {
        fprintf(stderr, "wvw: failed to create window\n");
        kc_wvw_options_free(&opts);
        return 1;
    }

    if (bridge_enabled) {
        const char *allowed[] = {
            "ping", "echo", "getStats", "notifyHost", NULL
        };
        kc_wvw_bridge_options_t bopts;
        bopts.methods = allowed;
        bopts.method_count = 4;
        bopts.callback = kc_cli_bridge_callback;
        bopts.userdata = NULL;
        bopts.allow_file = 1;
        bopts.allow_data = 0;
        bopts.allow_localhost = 0;
        kc_wvw_enable_bridge(ctx, &bopts);
    }

    kc_wvw_listen_signals(ctx);
    kc_wvw_on_signal(ctx, SIGINT, kc_wvw_signal_cb);
    kc_wvw_on_signal(ctx, SIGTERM, kc_wvw_signal_cb);
#ifndef _WIN32
    kc_wvw_listen_signal(ctx, SIGINT);
    kc_wvw_listen_signal(ctx, SIGTERM);
#endif

    if (kc_wvw_run(ctx) != KC_WVW_OK) {
        fprintf(stderr, "wvw: runtime failed\n");
        kc_wvw_close(ctx);
        kc_wvw_options_free(&opts);
        return 1;
    }

    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return 0;
}
