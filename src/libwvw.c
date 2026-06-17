/**
 * libwvw.c - Core implementation for the wvw library.
 * Summary: Implements the non-Windows native WebView backend.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "wvw.h"

#include <gtk/gtk.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <webkit2/webkit2.h>

typedef enum {
    KC_ENV_TYPE_INT,
    KC_ENV_TYPE_STR,
} kc_env_type_t;

typedef struct {
    const char *env_var;
    size_t offset;
    kc_env_type_t type;
} kc_env_map_t;

typedef struct {
    int sig;
    kc_wvw_signal_callback_t cb;
} kc_wvw_signal_entry_t;

struct kc_wvw {
    kc_wvw_options_t opts;
    kc_wvw_signal_entry_t *signal_handlers;
    int n_signal_handlers;
    int signal_handlers_capacity;
    int running;
    GtkWidget *window;
    WebKitWebView *web_view;
};

static const kc_env_map_t env_config_table[] = {
    { "KC_WVW_URL", offsetof(kc_wvw_options_t, url), KC_ENV_TYPE_STR },
    { "KC_WVW_TITLE", offsetof(kc_wvw_options_t, title), KC_ENV_TYPE_STR },
    { "KC_WVW_WIDTH", offsetof(kc_wvw_options_t, width), KC_ENV_TYPE_INT },
    { "KC_WVW_HEIGHT", offsetof(kc_wvw_options_t, height), KC_ENV_TYPE_INT },
    { "KC_WVW_FULLSCREEN", offsetof(kc_wvw_options_t, fullscreen), KC_ENV_TYPE_INT },
    { "KC_WVW_BORDERLESS", offsetof(kc_wvw_options_t, borderless), KC_ENV_TYPE_INT },
};

static const int env_config_table_n = sizeof(env_config_table) / sizeof(env_config_table[0]);
static kc_wvw_t *g_signal_ctx = NULL;

#ifndef KC_WVW_BUILD_VERSION
#define KC_WVW_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_wvw_version(void) {
    return (uint64_t)KC_WVW_BUILD_VERSION;
}

/**
 * Duplicate one C string.
 * @param text Source string.
 * @return Newly allocated duplicate or NULL.
 */
static char *kc_wvw_strdup(const char *text) {
    size_t length;
    char *copy;

    if (!text) {
        return NULL;
    }

    length = strlen(text);
    copy = (char *)malloc(length + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, text, length + 1);
    return copy;
}

/**
 * Close the GTK main loop when the native window is destroyed.
 * @param widget GTK widget.
 * @param userdata Window context.
 * @return None.
 */
static void kc_wvw_linux_destroy(GtkWidget *widget, gpointer userdata) {
    kc_wvw_t *ctx = (kc_wvw_t *)userdata;

    (void)widget;

    if (ctx) {
        ctx->running = 0;
    }
    gtk_main_quit();
}

/**
 * Create the native GTK window and WebView.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_linux_create_window(kc_wvw_t *ctx) {
    ctx->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    if (!ctx->window) {
        return KC_WVW_ERROR;
    }

    gtk_window_set_default_size(GTK_WINDOW(ctx->window), ctx->opts.width, ctx->opts.height);
    gtk_window_set_title(GTK_WINDOW(ctx->window), ctx->opts.title ? ctx->opts.title : "wvw");
    if (ctx->opts.borderless || ctx->opts.fullscreen) {
        gtk_window_set_decorated(GTK_WINDOW(ctx->window), FALSE);
    }
    if (ctx->opts.fullscreen) {
        gtk_window_fullscreen(GTK_WINDOW(ctx->window));
    }

    ctx->web_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    if (!ctx->web_view) {
        return KC_WVW_ERROR;
    }

    gtk_container_add(GTK_CONTAINER(ctx->window), GTK_WIDGET(ctx->web_view));
    g_signal_connect(ctx->window, "destroy", G_CALLBACK(kc_wvw_linux_destroy), ctx);
    gtk_widget_show_all(ctx->window);
    return KC_WVW_OK;
}

/**
 * Create an options struct initialized with default values.
 * @return Default-initialized options.
 */
kc_wvw_options_t kc_wvw_options_default(void) {
    kc_wvw_options_t opts;

    memset(&opts, 0, sizeof(opts));
    opts.width = 1280;
    opts.height = 720;
    return opts;
}

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_wvw_options_load_env(kc_wvw_options_t *opts) {
    int i;

    if (!opts) {
        return;
    }

    for (i = 0; i < env_config_table_n; i++) {
        const char *val = getenv(env_config_table[i].env_var);
        if (!val) {
            continue;
        }
        if (env_config_table[i].type == KC_ENV_TYPE_INT) {
            char *end;
            long value = strtol(val, &end, 10);
            if (end != val && *end == '\0') {
                *(int *)((char *)opts + env_config_table[i].offset) = (int)value;
            }
        } else {
            char **slot = (char **)((char *)opts + env_config_table[i].offset);
            free(*slot);
            *slot = kc_wvw_strdup(val);
        }
    }
}

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_wvw_options_free(kc_wvw_options_t *opts) {
    if (!opts) {
        return;
    }

    free(opts->url);
    free(opts->title);
    opts->url = NULL;
    opts->title = NULL;
}

/**
 * Register a handler for a library-level signal number.
 * @param ctx Window context.
 * @param sig Application-defined signal number.
 * @param cb Callback to invoke.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_on_signal(kc_wvw_t *ctx, int sig, kc_wvw_signal_callback_t cb) {
    int i;

    if (!ctx) {
        return KC_WVW_ERROR;
    }

    for (i = 0; i < ctx->n_signal_handlers; i++) {
        if (ctx->signal_handlers[i].sig == sig) {
            if (cb) {
                ctx->signal_handlers[i].cb = cb;
            } else {
                int tail = ctx->n_signal_handlers - i - 1;
                if (tail > 0) {
                    memmove(&ctx->signal_handlers[i], &ctx->signal_handlers[i + 1], (size_t)tail * sizeof(kc_wvw_signal_entry_t));
                }
                ctx->n_signal_handlers--;
            }
            return KC_WVW_OK;
        }
    }

    if (!cb) {
        return KC_WVW_OK;
    }

    if (ctx->n_signal_handlers >= ctx->signal_handlers_capacity) {
        int new_capacity = ctx->signal_handlers_capacity ? ctx->signal_handlers_capacity * 2 : 4;
        kc_wvw_signal_entry_t *next_handlers = (kc_wvw_signal_entry_t *)realloc(ctx->signal_handlers, (size_t)new_capacity * sizeof(kc_wvw_signal_entry_t));
        if (!next_handlers) {
            return KC_WVW_ERROR;
        }
        ctx->signal_handlers = next_handlers;
        ctx->signal_handlers_capacity = new_capacity;
    }

    ctx->signal_handlers[ctx->n_signal_handlers].sig = sig;
    ctx->signal_handlers[ctx->n_signal_handlers].cb = cb;
    ctx->n_signal_handlers++;
    return KC_WVW_OK;
}

/**
 * Raise a library-level signal.
 * @param ctx Window context.
 * @param sig Signal number to raise.
 * @return KC_WVW_OK if handled or KC_WVW_ERROR if no handler exists.
 */
int kc_wvw_raise_signal(kc_wvw_t *ctx, int sig) {
    int i;

    if (!ctx) {
        return KC_WVW_ERROR;
    }

    for (i = 0; i < ctx->n_signal_handlers; i++) {
        if (ctx->signal_handlers[i].sig == sig) {
            ctx->signal_handlers[i].cb(ctx);
            return KC_WVW_OK;
        }
    }

    return KC_WVW_ERROR;
}

/**
 * Store the context for later signal dispatch.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_listen_signals(kc_wvw_t *ctx) {
    if (!ctx) {
        return KC_WVW_ERROR;
    }
    g_signal_ctx = ctx;
    return KC_WVW_OK;
}

/**
 * Connect one operating-system signal to the library dispatcher.
 * @param ctx Window context.
 * @param sig_id Operating-system signal number.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_listen_signal(kc_wvw_t *ctx, int sig_id) {
    if (!ctx) {
        return KC_WVW_ERROR;
    }
    g_signal_ctx = ctx;
    signal(sig_id, kc_wvw_signal_listener);
    return KC_WVW_OK;
}

/**
 * Dispatch an operating-system signal into the library signal table.
 * @param sig Operating-system signal number.
 * @return None.
 */
void kc_wvw_signal_listener(int sig) {
    if (g_signal_ctx && kc_wvw_raise_signal(g_signal_ctx, sig) == KC_WVW_OK) {
        return;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

/**
 * Create a new native WebView context.
 * @param ctx_out Destination context pointer.
 * @param opts Configuration options.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_open(kc_wvw_t **ctx_out, kc_wvw_options_t *opts) {
    kc_wvw_t *ctx;

    if (!ctx_out || !opts || !opts->url) {
        return KC_WVW_ERROR;
    }

    ctx = (kc_wvw_t *)calloc(1, sizeof(kc_wvw_t));
    if (!ctx) {
        return KC_WVW_ERROR;
    }

    ctx->opts = *opts;
    ctx->opts.url = kc_wvw_strdup(opts->url);
    ctx->opts.title = opts->title ? kc_wvw_strdup(opts->title) : kc_wvw_strdup("wvw");
    ctx->opts.width = opts->width;
    ctx->opts.height = opts->height;
    ctx->opts.fullscreen = opts->fullscreen;
    ctx->opts.borderless = opts->borderless;

    if (!ctx->opts.url || !ctx->opts.title) {
        kc_wvw_close(ctx);
        return KC_WVW_ERROR;
    }

    if (!gtk_init_check(NULL, NULL)) {
        kc_wvw_close(ctx);
        return KC_WVW_ERROR;
    }
    if (kc_wvw_linux_create_window(ctx) != KC_WVW_OK) {
        kc_wvw_close(ctx);
        return KC_WVW_ERROR;
    }

    *ctx_out = ctx;
    return KC_WVW_OK;
}

/**
 * Release a WebView context and its resources.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_close(kc_wvw_t *ctx) {
    if (!ctx) {
        return KC_WVW_OK;
    }

    if (ctx->window) {
        gtk_widget_destroy(ctx->window);
    }

    free(ctx->signal_handlers);
    kc_wvw_options_free(&ctx->opts);
    free(ctx);
    return KC_WVW_OK;
}

/**
 * Start the native window event loop.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_run(kc_wvw_t *ctx) {
    if (!ctx) {
        return KC_WVW_ERROR;
    }

    ctx->running = 1;
    if (kc_wvw_navigate(ctx, ctx->opts.url) != KC_WVW_OK) {
        return KC_WVW_ERROR;
    }

    gtk_main();
    return KC_WVW_OK;
}

/**
 * Navigate the current WebView to a new URL.
 * @param ctx Window context.
 * @param url Destination URL.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_navigate(kc_wvw_t *ctx, const char *url) {
    if (!ctx || !url) {
        return KC_WVW_ERROR;
    }

    webkit_web_view_load_uri(ctx->web_view, url);
    return KC_WVW_OK;
}
