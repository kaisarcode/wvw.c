/**
 * macos.m - macOS WebKit backend for wvw.
 * Uses WKWebView via Objective-C runtime.
 */

#import <Foundation/Foundation.h>
#import <WebKit/WebKit.h>

#include "libwvw.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    char **methods;
    int method_count;
    kc_wvw_bridge_callback_t callback;
    void *userdata;
    int allow_file;
    int allow_data;
    int allow_localhost;
} kc_wvw_bridge_state_t;

struct kc_wvw {
    kc_wvw_options_t opts;
    int running;
    int closing;
    void *ns_window;
    void *ns_webview;
    kc_wvw_bridge_state_t bridge;
};

static const kc_env_map_t env_config_table[] = {
    { "KC_WVW_URL", offsetof(kc_wvw_options_t, url), KC_ENV_TYPE_STR },
    { "KC_WVW_TITLE", offsetof(kc_wvw_options_t, title), KC_ENV_TYPE_STR },
    { "KC_WVW_BACKGROUND", offsetof(kc_wvw_options_t, background), KC_ENV_TYPE_STR },
    { "KC_WVW_WIDTH", offsetof(kc_wvw_options_t, width), KC_ENV_TYPE_INT },
    { "KC_WVW_HEIGHT", offsetof(kc_wvw_options_t, height), KC_ENV_TYPE_INT },
    { "KC_WVW_FULLSCREEN", offsetof(kc_wvw_options_t, fullscreen), KC_ENV_TYPE_INT },
    { "KC_WVW_BORDERLESS", offsetof(kc_wvw_options_t, borderless), KC_ENV_TYPE_INT },
    { "KC_WVW_ALWAYS_ON_TOP", offsetof(kc_wvw_options_t, always_on_top), KC_ENV_TYPE_INT },
    { "KC_WVW_CLICK_THROUGH", offsetof(kc_wvw_options_t, click_through), KC_ENV_TYPE_INT },
    { "KC_WVW_NO_FOCUS", offsetof(kc_wvw_options_t, no_focus), KC_ENV_TYPE_INT },
};

static const int env_config_table_n = sizeof(env_config_table) / sizeof(env_config_table[0]);
#ifndef KC_WVW_BUILD_VERSION
#define KC_WVW_BUILD_VERSION 0ULL
#endif

static void kc_wvw_env_load(kc_wvw_options_t *opts) {
    for (int i = 0; i < env_config_table_n; i++) {
        const char *val = getenv(env_config_table[i].env_var);
        if (!val) continue;
        if (env_config_table[i].type == KC_ENV_TYPE_STR) {
            char **field = (char **)((char *)opts + env_config_table[i].offset);
            free(*field);
            *field = strdup(val);
        } else if (env_config_table[i].type == KC_ENV_TYPE_INT) {
            int *field = (int *)((char *)opts + env_config_table[i].offset);
            *field = atoi(val);
        }
    }
}

kc_wvw_options_t kc_wvw_options_default(void) {
    kc_wvw_options_t opts;
    memset(&opts, 0, sizeof(opts));
    return opts;
}

void kc_wvw_options_load_env(kc_wvw_options_t *opts) {
    if (!opts) return;
    kc_wvw_env_load(opts);
}

void kc_wvw_options_free(kc_wvw_options_t *opts) {
    if (!opts) return;
    free(opts->url);
    free(opts->title);
    free(opts->background);
}

uint64_t kc_wvw_version(void) { return (uint64_t)KC_WVW_BUILD_VERSION; }

int kc_wvw_open(kc_wvw_t **out, kc_wvw_options_t *opts) {
    if (!out || !opts) return KC_WVW_ERROR;
    kc_wvw_t *ctx = (kc_wvw_t *)calloc(1, sizeof(kc_wvw_t));
    if (!ctx) return KC_WVW_ERROR;
    ctx->opts = *opts;
    if (opts->url) ctx->opts.url = strdup(opts->url);
    if (opts->title) ctx->opts.title = strdup(opts->title);
    if (opts->background) ctx->opts.background = strdup(opts->background);
    *out = ctx;
    return KC_WVW_OK;
}

int kc_wvw_navigate(kc_wvw_t *ctx, const char *url) {
    if (!ctx || !url) return KC_WVW_ERROR;
    @autoreleasepool {
        WKWebView *webView = (__bridge WKWebView *)ctx->ns_webview;
        NSString *nsUrl = [NSString stringWithUTF8String:url];
        NSURLRequest *request = [NSURLRequest requestWithURL:[NSURL URLWithString:nsUrl]];
        [webView loadRequest:request];
    }
    return KC_WVW_OK;
}

int kc_wvw_exec(kc_wvw_t *ctx, const char *js) {
    if (!ctx || !js) return KC_WVW_ERROR;
    @autoreleasepool {
        WKWebView *webView = (__bridge WKWebView *)ctx->ns_webview;
        NSString *nsJs = [NSString stringWithUTF8String:js];
        [webView evaluateJavaScript:nsJs completionHandler:nil];
    }
    return KC_WVW_OK;
}

int kc_wvw_stop(kc_wvw_t *ctx) {
    if (!ctx) return KC_WVW_ERROR;
    ctx->closing = 1;
    return KC_WVW_OK;
}

int kc_wvw_enable_bridge(kc_wvw_t *ctx, const kc_wvw_bridge_options_t *opts) {
    (void)ctx; (void)opts;
    return KC_WVW_OK;
}

int kc_wvw_post_bridge_event(kc_wvw_t *ctx, const char *json) {
    if (!ctx || !json) return KC_WVW_ERROR;
    @autoreleasepool {
        WKWebView *webView = (__bridge WKWebView *)ctx->ns_webview;
        NSString *nsJson = [NSString stringWithUTF8String:json];
        NSString *js = [NSString stringWithFormat:@"window.kc_wvw_bridge_dispatch('%@')", nsJson];
        [webView evaluateJavaScript:js completionHandler:nil];
    }
    return KC_WVW_OK;
}

int kc_wvw_close(kc_wvw_t *ctx) {
    if (!ctx) return KC_WVW_ERROR;
    free(ctx->opts.url);
    free(ctx->opts.title);
    free(ctx->opts.background);
    free(ctx);
    return KC_WVW_OK;
}

static int kc_wvw_macos_create_window(kc_wvw_t *ctx) {
    @autoreleasepool {
        WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];
        WKWebView *webView = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:config];

        NSString *url = ctx->opts.url ? [NSString stringWithUTF8String:ctx->opts.url] : @"about:blank";
        NSURLRequest *request = [NSURLRequest requestWithURL:[NSURL URLWithString:url]];
        [webView loadRequest:request];

        NSRect frame = NSMakeRect(0, 0,
            ctx->opts.width > 0 ? ctx->opts.width : 800,
            ctx->opts.height > 0 ? ctx->opts.height : 600);

        NSString *title = ctx->opts.title ? [NSString stringWithUTF8String:ctx->opts.title] : @"wvw";
        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:frame
            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
            backing:NSBackingStoreBuffered
            defer:NO];
        [window setTitle:title];
        [window setContentView:webView];

        if (ctx->opts.fullscreen) [window toggleFullScreen:nil];
        if (ctx->opts.borderless) [window setStyleMask:NSWindowStyleMaskBorderless];
        if (ctx->opts.always_on_top) [window setLevel:NSFloatingWindowLevel];

        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        ctx->ns_window = (void *)CFBridgingRetain(window);
        ctx->ns_webview = (void *)CFBridgingRetain(webView);
        return KC_WVW_OK;
    }
}

int kc_wvw_show(kc_wvw_t *ctx) {
    if (!ctx) return KC_WVW_ERROR;
    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        [window orderFront:nil];
    }
    return KC_WVW_OK;
}

int kc_wvw_hide(kc_wvw_t *ctx) {
    if (!ctx) return KC_WVW_ERROR;
    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        [window orderOut:nil];
    }
    return KC_WVW_OK;
}

int kc_wvw_minimize(kc_wvw_t *ctx) {
    if (!ctx) return KC_WVW_ERROR;
    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        [window miniaturize:nil];
    }
    return KC_WVW_OK;
}

int kc_wvw_tray_init(kc_wvw_t *ctx, const char *tooltip, const char *icon) {
    (void)ctx; (void)tooltip; (void)icon;
    return KC_WVW_OK;
}

int kc_wvw_tray_remove(kc_wvw_t *ctx) {
    (void)ctx;
    return KC_WVW_OK;
}

int kc_wvw_tray_set_menu(kc_wvw_t *ctx, const kc_wvw_tray_item_t *items, int count) {
    (void)ctx; (void)items; (void)count;
    return KC_WVW_OK;
}

int kc_wvw_open_window(kc_wvw_t *ctx) {
    if (!ctx) return KC_WVW_ERROR;
    return kc_wvw_macos_create_window(ctx);
}

int kc_wvw_run(kc_wvw_t *ctx) {
    if (!ctx) return KC_WVW_ERROR;
    ctx->running = 1;
    @autoreleasepool {
        [NSApp run];
    }
    return KC_WVW_OK;
}
