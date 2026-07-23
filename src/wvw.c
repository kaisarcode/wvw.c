/**
 * wvw.c - Native WebView window runner.
 * Summary: Command line interface for opening one native WebView window.
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
    printf("    --bridge          Enable NativeBridge.window and NativeBridge.invoke\n");
    printf("    --tray [icon]     Hide to system tray. Optional icon name (Linux) or\n");
    printf("                     .ico path (Windows). Defaults to a system icon.\n");
    printf("    -h, --help        Show this help\n");
    printf("    -v, --version     Show build version\n");
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
    int tray_enabled = 0;
    char *tray_icon = NULL;

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
        } else if (strcmp(argv[i], "--tray") == 0) {
            tray_enabled = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                i++;
                free(tray_icon);
                tray_icon = strdup(argv[i]);
            }
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
        free(tray_icon);
        return 1;
    }

    if (kc_wvw_open(&ctx, &opts) != KC_WVW_OK) {
        fprintf(stderr, "wvw: failed to create window\n");
        kc_wvw_options_free(&opts);
        free(tray_icon);
        return 1;
    }

    if (bridge_enabled) {
        const char *allowed[] = { NULL };
        kc_wvw_bridge_options_t bopts;
        bopts.methods = allowed;
        bopts.method_count = 0;
        bopts.callback = NULL;
        bopts.userdata = NULL;
        bopts.allow_file = 1;
        bopts.allow_data = 0;
        bopts.allow_localhost = 0;
        if (kc_wvw_enable_bridge(ctx, &bopts) != KC_WVW_OK) {
            fprintf(stderr, "wvw: bridge init failed\n");
            kc_wvw_close(ctx);
            kc_wvw_options_free(&opts);
            return 1;
        }
    }

    {
        const char *tray_env = getenv("KC_WVW_TRAY");
        if (tray_env && tray_env[0] == '1') {
            tray_enabled = 1;
        }
    }
    if (tray_enabled) {
        if (kc_wvw_tray_init(ctx, opts.title ? opts.title : "wvw", tray_icon) != KC_WVW_OK) {
            fprintf(stderr, "wvw: tray init failed\n");
            kc_wvw_close(ctx);
            kc_wvw_options_free(&opts);
            return 1;
        }
    }

    if (kc_wvw_run(ctx) != KC_WVW_OK) {
        fprintf(stderr, "wvw: runtime failed\n");
        kc_wvw_close(ctx);
        kc_wvw_options_free(&opts);
        return 1;
    }

    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    free(tray_icon);
    return 0;
}
