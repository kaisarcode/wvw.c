/**
 * macos.m - macOS WebKit backend for wvw.
 * Summary: Implements native WebView backend using Cocoa and WKWebView.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#import <WebKit/WebKit.h>

#include "libwvw.h"
#include <objc/runtime.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KC_WVW_BRIDGE_MAX_MESSAGE 65536

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
    int tray_enabled;
    void *ns_status_item;
    kc_wvw_tray_item_t *tray_items;
    int tray_count;
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

static int kc_wvw_bridge_url_trusted(kc_wvw_t *ctx, kc_wvw_bridge_state_t *bridge, const char *url);
static void kc_wvw_bridge_state_free(kc_wvw_bridge_state_t *bridge);
static int kc_wvw_bridge_state_copy(kc_wvw_bridge_state_t *dst, const kc_wvw_bridge_options_t *src);
static char *kc_wvw_bridge_bootstrap_script(kc_wvw_bridge_state_t *bridge);
static int kc_wvw_bridge_post_json(kc_wvw_t *ctx, const char *json);
static char *kc_wvw_bridge_dispatch_request(kc_wvw_t *ctx, const char *json);
static char *kc_wvw_bridge_param_string(const char *params, const char *key);
static int kc_wvw_bridge_param_int(const char *params, const char *key, int *out);

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

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} kc_wvw_text_buf_t;

static int kc_wvw_text_buf_init(kc_wvw_text_buf_t *buf) {
    if (!buf) {
        return KC_WVW_ERROR;
    }

    buf->data = (char *)malloc(256);
    if (!buf->data) {
        return KC_WVW_ERROR;
    }

    buf->data[0] = '\0';
    buf->len = 0;
    buf->cap = 256;
    return KC_WVW_OK;
}

static int kc_wvw_text_buf_append_n(kc_wvw_text_buf_t *buf, const char *text, size_t len) {
    char *next;
    size_t cap;

    if (!buf || !buf->data || (!text && len != 0)) {
        return KC_WVW_ERROR;
    }

    if (buf->len + len + 1 > buf->cap) {
        cap = buf->cap;
        while (buf->len + len + 1 > cap) {
            cap *= 2;
        }
        next = (char *)realloc(buf->data, cap);
        if (!next) {
            return KC_WVW_ERROR;
        }
        buf->data = next;
        buf->cap = cap;
    }

    if (len > 0) {
        memcpy(buf->data + buf->len, text, len);
        buf->len += len;
    }
    buf->data[buf->len] = '\0';
    return KC_WVW_OK;
}

static int kc_wvw_text_buf_append(kc_wvw_text_buf_t *buf, const char *text) {
    if (!text) {
        return KC_WVW_ERROR;
    }

    return kc_wvw_text_buf_append_n(buf, text, strlen(text));
}

static char *kc_wvw_strndup_range(const char *start, size_t len) {
    char *copy;

    if (!start) {
        return NULL;
    }

    copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

static int kc_wvw_bridge_method_valid(const char *method) {
    size_t i;

    if (!method || !method[0]) {
        return 0;
    }

    if (!((method[0] >= 'A' && method[0] <= 'Z') || (method[0] >= 'a' && method[0] <= 'z') || method[0] == '_' || method[0] == '$')) {
        return 0;
    }

    for (i = 1; method[i]; i++) {
        if (!((method[i] >= 'A' && method[i] <= 'Z') || (method[i] >= 'a' && method[i] <= 'z') || (method[i] >= '0' && method[i] <= '9') || method[i] == '_' || method[i] == '$')) {
            return 0;
        }
    }

    return 1;
}

static char *kc_wvw_extract_origin(const char *url) {
    const char *p;
    const char *end;
    size_t len;

    if (!url) {
        return NULL;
    }
    p = strstr(url, "://");
    if (!p) {
        return NULL;
    }
    p += 3;
    end = p;
    while (*end && *end != '/' && *end != '?' && *end != '#') {
        end++;
    }
    len = (size_t)(end - url);
    return kc_wvw_strndup_range(url, len);
}

static int kc_wvw_is_origin_in_list(const char *origin, const char *list) {
    const char *p;
    const char *next;
    size_t len;
    size_t origin_len;

    if (!origin || !list) {
        return 0;
    }
    origin_len = strlen(origin);
    p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (!*p) {
            break;
        }
        next = p;
        while (*next && *next != ' ' && *next != '\t' && *next != '\r' && *next != '\n') {
            next++;
        }
        len = (size_t)(next - p);
        if (len == origin_len && strncmp(p, origin, len) == 0) {
            return 1;
        }
        p = next;
    }
    return 0;
}

static int kc_wvw_bridge_url_trusted(kc_wvw_t *ctx, kc_wvw_bridge_state_t *bridge, const char *url) {
    char *url_origin;
    char *init_origin;
    const char *env_trusted;
    int trusted = 0;

    if (!bridge || !bridge->callback || !url) {
        return 0;
    }

    if (bridge->allow_file && strncmp(url, "file://", 7) == 0) {
        return 1;
    }
    if (bridge->allow_data && strncmp(url, "data:", 5) == 0) {
        return 1;
    }
    if (bridge->allow_localhost && (strncmp(url, "http://localhost", 16) == 0 || strncmp(url, "https://localhost", 17) == 0)) {
        return 1;
    }

    url_origin = kc_wvw_extract_origin(url);
    if (!url_origin) {
        return 0;
    }

    env_trusted = getenv("TRUSTED_ORIGINS");
    if (env_trusted && kc_wvw_is_origin_in_list(url_origin, env_trusted)) {
        trusted = 1;
    }

    if (!trusted && ctx && ctx->opts.url) {
        init_origin = kc_wvw_extract_origin(ctx->opts.url);
        if (init_origin) {
            if (strcmp(url_origin, init_origin) == 0) {
                trusted = 1;
            }
            free(init_origin);
        }
    }

    free(url_origin);
    return trusted;
}

static void kc_wvw_bridge_state_free(kc_wvw_bridge_state_t *bridge) {
    int i;

    if (!bridge) {
        return;
    }

    if (bridge->methods) {
        for (i = 0; i < bridge->method_count; i++) {
            free(bridge->methods[i]);
        }
        free(bridge->methods);
    }

    memset(bridge, 0, sizeof(*bridge));
}

static int kc_wvw_bridge_state_copy(kc_wvw_bridge_state_t *dst, const kc_wvw_bridge_options_t *src) {
    int i;

    if (!dst || !src || !src->methods || src->method_count <= 0 || !src->callback) {
        return KC_WVW_ERROR;
    }

    dst->methods = (char **)calloc((size_t)src->method_count, sizeof(char *));
    if (!dst->methods) {
        return KC_WVW_ERROR;
    }

    dst->method_count = src->method_count;
    for (i = 0; i < src->method_count; i++) {
        if (!kc_wvw_bridge_method_valid(src->methods[i])) {
            kc_wvw_bridge_state_free(dst);
            return KC_WVW_ERROR;
        }
        dst->methods[i] = kc_wvw_strdup(src->methods[i]);
        if (!dst->methods[i]) {
            kc_wvw_bridge_state_free(dst);
            return KC_WVW_ERROR;
        }
    }

    dst->callback = src->callback;
    dst->userdata = src->userdata;
    dst->allow_file = src->allow_file;
    dst->allow_data = src->allow_data;
    dst->allow_localhost = src->allow_localhost;
    return KC_WVW_OK;
}

static int kc_wvw_bridge_method_allowed(kc_wvw_bridge_state_t *bridge, const char *method) {
    int i;

    if (!bridge || !method) {
        return 0;
    }

    for (i = 0; i < bridge->method_count; i++) {
        if (strcmp(bridge->methods[i], method) == 0) {
            return 1;
        }
    }

    return 0;
}

static char *kc_wvw_bridge_get_string(const char *json, const char *key) {
    char pattern[64];
    const char *start;
    const char *end;

    if (!json || !key) {
        return NULL;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    start = strstr(json, pattern);
    if (!start) {
        return NULL;
    }

    start += strlen(pattern);
    end = strchr(start, '"');
    if (!end) {
        return NULL;
    }

    return kc_wvw_strndup_range(start, (size_t)(end - start));
}

static char *kc_wvw_bridge_get_params(const char *json) {
    const char *start;
    const char *end;

    if (!json) {
        return NULL;
    }

    start = strstr(json, "\"params\":");
    if (!start) {
        return kc_wvw_strdup("null");
    }

    start += 9;
    end = strrchr(start, '}');
    if (!end) {
        return NULL;
    }

    return kc_wvw_strndup_range(start, (size_t)(end - start));
}

static char *kc_wvw_bridge_wrap_response(const char *id, int ok, const char *body) {
    kc_wvw_text_buf_t buf;

    if (!id || !body || kc_wvw_text_buf_init(&buf) != KC_WVW_OK) {
        return NULL;
    }

    if (kc_wvw_text_buf_append(&buf, "{\"id\":\"") != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, id) != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, ok ? "\",\"ok\":true,\"result\":" : "\",\"ok\":false,\"error\":") != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, body) != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, "}") != KC_WVW_OK) {
        free(buf.data);
        return NULL;
    }

    return buf.data;
}

static char *kc_wvw_bridge_error_object(const char *code, const char *message) {
    kc_wvw_text_buf_t buf;

    if (!code || !message || kc_wvw_text_buf_init(&buf) != KC_WVW_OK) {
        return NULL;
    }

    if (kc_wvw_text_buf_append(&buf, "{\"code\":\"") != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, code) != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, "\",\"message\":\"") != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, message) != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, "\"}") != KC_WVW_OK) {
        free(buf.data);
        return NULL;
    }

    return buf.data;
}

static char *kc_wvw_bridge_bootstrap_script(kc_wvw_bridge_state_t *bridge) {
    kc_wvw_text_buf_t buf;
    int i;

    if (!bridge || kc_wvw_text_buf_init(&buf) != KC_WVW_OK) {
        return NULL;
    }

    if (kc_wvw_text_buf_append(&buf, "(function(){if(window.NativeBridge){return;}var __kcWvwPending={};var __kcWvwSeq=0;function __kcWvwReceive(msg){if(msg&&typeof msg.id==='string'){var p=__kcWvwPending[msg.id];if(p){delete __kcWvwPending[msg.id];if(msg.ok){p.resolve(msg.result!==undefined?msg.result:{ok:true});}else{p.reject(msg.error||{code:'INTERNAL_ERROR',message:'Bridge error'});}}return;}window.dispatchEvent(new CustomEvent('") != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, KC_WVW_BRIDGE_EVENT_NAME) != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, "',{detail:msg}));}window.__kcWvwReceive=__kcWvwReceive;window.NativeBridge={};function __kcWvwSend(method,params){return new Promise(function(resolve,reject){var id=String(++__kcWvwSeq);__kcWvwPending[id]={resolve:resolve,reject:reject};window.webkit.messageHandlers.kc_wvw_native.postMessage(JSON.stringify({id:id,method:method,params:params===undefined?null:params}));});}window.NativeBridge.invoke=function(method,params){return __kcWvwSend(method,params);};window.NativeBridge.window={minimize:function(){return __kcWvwSend('window.minimize');},maximize:function(){return __kcWvwSend('window.maximize');},restore:function(){return __kcWvwSend('window.restore');},close:function(){return __kcWvwSend('window.close');},setTitle:function(title){return __kcWvwSend('window.setTitle',{title:title});},setSize:function(width,height){return __kcWvwSend('window.setSize',{width:width,height:height});},getState:function(){return __kcWvwSend('window.getState');}};") != KC_WVW_OK) {
        free(buf.data);
        return NULL;
    }

    for (i = 0; i < bridge->method_count; i++) {
        if (kc_wvw_text_buf_append(&buf, "window.NativeBridge.") != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, bridge->methods[i]) != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, "=function(params){return __kcWvwSend('") != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, bridge->methods[i]) != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, "',params);};") != KC_WVW_OK) {
            free(buf.data);
            return NULL;
        }
    }

    if (kc_wvw_text_buf_append(&buf, "}();") != KC_WVW_OK) {
        free(buf.data);
        return NULL;
    }

    return buf.data;
}

static char *kc_wvw_bridge_escape_js_string(const char *json) {
    kc_wvw_text_buf_t buf;
    size_t i;

    if (!json || kc_wvw_text_buf_init(&buf) != KC_WVW_OK) {
        return NULL;
    }

    for (i = 0; json[i]; i++) {
        if (json[i] == '\\' || json[i] == '\'') {
            if (kc_wvw_text_buf_append_n(&buf, "\\", 1) != KC_WVW_OK) {
                free(buf.data);
                return NULL;
            }
        }
        if (json[i] == '\n') {
            if (kc_wvw_text_buf_append(&buf, "\\n") != KC_WVW_OK) {
                free(buf.data);
                return NULL;
            }
            continue;
        }
        if (json[i] == '\r') {
            if (kc_wvw_text_buf_append(&buf, "\\r") != KC_WVW_OK) {
                free(buf.data);
                return NULL;
            }
            continue;
        }
        if (kc_wvw_text_buf_append_n(&buf, &json[i], 1) != KC_WVW_OK) {
            free(buf.data);
            return NULL;
        }
    }

    return buf.data;
}

static int kc_wvw_bridge_post_json(kc_wvw_t *ctx, const char *json) {
    char *escaped;
    char *script;
    size_t cap;

    if (!ctx || !ctx->ns_webview || !json || strlen(json) > KC_WVW_BRIDGE_MAX_MESSAGE) {
        return KC_WVW_ERROR;
    }

    escaped = kc_wvw_bridge_escape_js_string(json);
    if (!escaped) {
        return KC_WVW_ERROR;
    }

    cap = strlen(escaped) + 64;
    script = (char *)malloc(cap);
    if (!script) {
        free(escaped);
        return KC_WVW_ERROR;
    }

    snprintf(script, cap, "window.__kcWvwReceive(JSON.parse('%s'));", escaped);

    @autoreleasepool {
        WKWebView *webView = (__bridge WKWebView *)ctx->ns_webview;
        NSString *nsScript = [NSString stringWithUTF8String:script];
        [webView evaluateJavaScript:nsScript completionHandler:nil];
    }

    free(escaped);
    free(script);
    return KC_WVW_OK;
}

static char *kc_wvw_bridge_dispatch_request(kc_wvw_t *ctx, const char *json) {
    char *id;
    char *method;
    char *params;
    char *result;
    char *error;
    char *response;
    int rc;

    if (!ctx || !json || strlen(json) > KC_WVW_BRIDGE_MAX_MESSAGE) {
        error = kc_wvw_bridge_error_object("INVALID_REQUEST", "Bridge request is invalid.");
        response = error ? kc_wvw_bridge_wrap_response("0", 0, error) : NULL;
        free(error);
        return response;
    }

    id = kc_wvw_bridge_get_string(json, "id");
    method = kc_wvw_bridge_get_string(json, "method");
    params = kc_wvw_bridge_get_params(json);
    if (!id || !method || !params) {
        free(id);
        free(method);
        free(params);
        error = kc_wvw_bridge_error_object("INVALID_REQUEST", "Bridge request is malformed.");
        response = error ? kc_wvw_bridge_wrap_response("0", 0, error) : NULL;
        free(error);
        return response;
    }

    if (strcmp(method, "window.minimize") == 0) {
        rc = kc_wvw_minimize(ctx);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "window.maximize") == 0) {
        rc = kc_wvw_maximize(ctx);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "window.restore") == 0) {
        rc = kc_wvw_restore(ctx);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "window.close") == 0) {
        kc_wvw_close(ctx);
        result = kc_wvw_strdup("{\"ok\":true}");
        response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "window.setTitle") == 0) {
        char *title = kc_wvw_bridge_param_string(params, "title");
        if (!title) {
            error = kc_wvw_bridge_error_object("INVALID_ARGUMENT", "title must be a string.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
            free(id); free(method); free(params);
            return response;
        }
        if (strlen(title) > KC_WVW_TITLE_MAX) {
            free(title);
            error = kc_wvw_bridge_error_object("INVALID_ARGUMENT", "title exceeds maximum length.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
            free(id); free(method); free(params);
            return response;
        }
        rc = kc_wvw_set_title(ctx, title);
        free(title);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "window.setSize") == 0) {
        int width = 0, height = 0;
        if (kc_wvw_bridge_param_int(params, "width", &width) != KC_WVW_OK ||
            kc_wvw_bridge_param_int(params, "height", &height) != KC_WVW_OK) {
            error = kc_wvw_bridge_error_object("INVALID_ARGUMENT", "width and height must be integers.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
            free(id); free(method); free(params);
            return response;
        }
        if (width <= 0 || height <= 0 || width > KC_WVW_SIZE_MAX || height > KC_WVW_SIZE_MAX) {
            error = kc_wvw_bridge_error_object("INVALID_ARGUMENT", "width and height must be positive integers within bounds.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
            free(id); free(method); free(params);
            return response;
        }
        rc = kc_wvw_set_size(ctx, width, height);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "window.getState") == 0) {
        kc_wvw_window_state_t st;
        char buf[256];
        rc = kc_wvw_get_state(ctx, &st);
        if (rc == KC_WVW_OK) {
            snprintf(buf, sizeof(buf),
                "{\"ok\":true,\"state\":{\"width\":%d,\"height\":%d,\"minimized\":%s,\"maximized\":%s,\"fullscreen\":%s,\"visible\":%s}}",
                st.width, st.height,
                st.minimized ? "true" : "false",
                st.maximized ? "true" : "false",
                st.fullscreen ? "true" : "false",
                st.visible ? "true" : "false");
            result = kc_wvw_strdup(buf);
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }

    if (!kc_wvw_bridge_method_allowed(&ctx->bridge, method)) {
        error = kc_wvw_bridge_error_object("METHOD_NOT_FOUND", "Bridge method is not allowed.");
        response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
        free(id);
        free(method);
        free(params);
        free(error);
        return response;
    }

    result = NULL;
    rc = ctx->bridge.callback(ctx, method, params, &result, ctx->bridge.userdata);
    if (rc == KC_WVW_OK) {
        if (!result) {
            result = kc_wvw_strdup("null");
        }
        response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
    } else {
        if (!result) {
            result = kc_wvw_bridge_error_object("OPERATION_FAILED", "Bridge callback failed.");
        }
        response = result ? kc_wvw_bridge_wrap_response(id, 0, result) : NULL;
    }

    free(id);
    free(method);
    free(params);
    free(result);
    return response;
}

static int kc_wvw_parse_background_color(const char *text, CGFloat *out_r, CGFloat *out_g, CGFloat *out_b, CGFloat *out_a) {
    size_t length;
    unsigned int parts[4];

    if (!text || !out_r || !out_g || !out_b || !out_a) {
        return KC_WVW_ERROR;
    }

    length = strlen(text);
    if (length == 6) {
        if (sscanf(text, "%2x%2x%2x", &parts[1], &parts[2], &parts[3]) != 3) {
            return KC_WVW_ERROR;
        }
        parts[0] = 0xff;
    } else if (length == 8) {
        if (sscanf(text, "%2x%2x%2x%2x", &parts[0], &parts[1], &parts[2], &parts[3]) != 4) {
            return KC_WVW_ERROR;
        }
    } else {
        return KC_WVW_ERROR;
    }

    *out_r = (CGFloat)parts[1] / 255.0;
    *out_g = (CGFloat)parts[2] / 255.0;
    *out_b = (CGFloat)parts[3] / 255.0;
    *out_a = (CGFloat)parts[0] / 255.0;
    return KC_WVW_OK;
}

static int kc_wvw_background_transparent(const char *text) {
    CGFloat r, g, b, a;

    if (!text) {
        return 0;
    }
    if (kc_wvw_parse_background_color(text, &r, &g, &b, &a) != KC_WVW_OK) {
        return 0;
    }

    return a <= 0.0;
}

#pragma mark - ObjC bridge delegates

@interface KCWvwScriptMessageHandler : NSObject <WKScriptMessageHandler>
@property (nonatomic, assign) kc_wvw_t *ctx;
@end

@implementation KCWvwScriptMessageHandler
- (void)userContentController:(WKUserContentController *)userContentController
      didReceiveScriptMessage:(WKScriptMessage *)message {
    (void)userContentController;
    if (!self.ctx) {
        return;
    }

    NSString *body = message.body;
    if (![body isKindOfClass:[NSString class]]) {
        return;
    }

    const char *utf8 = [body UTF8String];
    if (!utf8 || strlen(utf8) > KC_WVW_BRIDGE_MAX_MESSAGE) {
        return;
    }

    char *response = kc_wvw_bridge_dispatch_request(self.ctx, utf8);
    if (response) {
        kc_wvw_bridge_post_json(self.ctx, response);
        free(response);
    }
}
@end

@interface KCWvwNavigationDelegate : NSObject <WKNavigationDelegate>
@property (nonatomic, assign) kc_wvw_t *ctx;
@end

@implementation KCWvwNavigationDelegate
- (void)webView:(WKWebView *)webView
    decidePolicyForNavigationAction:(WKNavigationAction *)navigationAction
                   decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    (void)webView;
    if (!self.ctx || !self.ctx->bridge.callback) {
        decisionHandler(WKNavigationActionPolicyAllow);
        return;
    }

    NSURL *url = navigationAction.request.URL;
    if (url) {
        const char *urlStr = [[url absoluteString] UTF8String];
        if (urlStr && !kc_wvw_bridge_url_trusted(self.ctx, &self.ctx->bridge, urlStr)) {
            decisionHandler(WKNavigationActionPolicyCancel);
            return;
        }
    }

    decisionHandler(WKNavigationActionPolicyAllow);
}
@end

@interface KCWvwWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) kc_wvw_t *ctx;
@end

@implementation KCWvwWindowDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender {
    (void)sender;
    if (!self.ctx) {
        return YES;
    }

    if (self.ctx->tray_enabled) {
        [sender orderOut:nil];
        return NO;
    }

    return YES;
}

- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    if (!self.ctx) {
        return;
    }
    self.ctx->running = 0;
}
@end

#pragma mark - Public API

static void kc_wvw_env_load(kc_wvw_options_t *opts) {
    int i;

    for (i = 0; i < env_config_table_n; i++) {
        const char *val = getenv(env_config_table[i].env_var);
        if (!val) {
            continue;
        }
        if (env_config_table[i].type == KC_ENV_TYPE_STR) {
            char **field = (char **)((char *)opts + env_config_table[i].offset);
            free(*field);
            *field = kc_wvw_strdup(val);
        } else if (env_config_table[i].type == KC_ENV_TYPE_INT) {
            char *end;
            long value = strtol(val, &end, 10);
            if (end != val && *end == '\0') {
                int *field = (int *)((char *)opts + env_config_table[i].offset);
                *field = (int)value;
            }
        }
    }
}

kc_wvw_options_t kc_wvw_options_default(void) {
    kc_wvw_options_t opts;

    memset(&opts, 0, sizeof(opts));
    opts.width = 1280;
    opts.height = 720;
    return opts;
}

void kc_wvw_options_load_env(kc_wvw_options_t *opts) {
    if (!opts) {
        return;
    }
    kc_wvw_env_load(opts);
}

void kc_wvw_options_free(kc_wvw_options_t *opts) {
    if (!opts) {
        return;
    }

    free(opts->url);
    free(opts->title);
    free(opts->background);
    opts->url = NULL;
    opts->title = NULL;
    opts->background = NULL;
}

uint64_t kc_wvw_version(void) { return (uint64_t)KC_WVW_BUILD_VERSION; }

int kc_wvw_open(kc_wvw_t **out, kc_wvw_options_t *opts) {
    kc_wvw_t *ctx;

    if (!out || !opts || !opts->url) {
        return KC_WVW_ERROR;
    }

    ctx = (kc_wvw_t *)calloc(1, sizeof(kc_wvw_t));
    if (!ctx) {
        return KC_WVW_ERROR;
    }

    ctx->opts = *opts;
    ctx->opts.url = kc_wvw_strdup(opts->url);
    ctx->opts.title = opts->title ? kc_wvw_strdup(opts->title) : kc_wvw_strdup("wvw");
    ctx->opts.background = opts->background ? kc_wvw_strdup(opts->background) : NULL;
    ctx->opts.width = opts->width;
    ctx->opts.height = opts->height;
    ctx->opts.fullscreen = opts->fullscreen;
    ctx->opts.borderless = opts->borderless;
    ctx->opts.always_on_top = opts->always_on_top;
    ctx->opts.click_through = opts->click_through;
    ctx->opts.no_focus = opts->no_focus;

    if (!ctx->opts.url || !ctx->opts.title || (opts->background && !ctx->opts.background)) {
        kc_wvw_close(ctx);
        return KC_WVW_ERROR;
    }

    *out = ctx;
    return KC_WVW_OK;
}

int kc_wvw_navigate(kc_wvw_t *ctx, const char *url) {
    if (!ctx || !url) {
        return KC_WVW_ERROR;
    }

    if (ctx->bridge.callback && !kc_wvw_bridge_url_trusted(ctx, &ctx->bridge, url)) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        WKWebView *webView = (__bridge WKWebView *)ctx->ns_webview;
        if (!webView) {
            return KC_WVW_ERROR;
        }
        NSString *nsUrl = [NSString stringWithUTF8String:url];
        NSURLRequest *request = [NSURLRequest requestWithURL:[NSURL URLWithString:nsUrl]];
        [webView loadRequest:request];
    }
    return KC_WVW_OK;
}

int kc_wvw_exec(kc_wvw_t *ctx, const char *js) {
    if (!ctx || !js) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        WKWebView *webView = (__bridge WKWebView *)ctx->ns_webview;
        if (!webView) {
            return KC_WVW_ERROR;
        }
        NSString *nsJs = [NSString stringWithUTF8String:js];
        [webView evaluateJavaScript:nsJs completionHandler:nil];
    }
    return KC_WVW_OK;
}

int kc_wvw_stop(kc_wvw_t *ctx) {
    if (!ctx) {
        return KC_WVW_ERROR;
    }

    ctx->closing = 1;
    ctx->running = 0;
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp stop:nil];
    });
    return KC_WVW_OK;
}

static int kc_wvw_macos_install_bridge(kc_wvw_t *ctx) {
    WKUserContentController *manager;
    KCWvwScriptMessageHandler *handler;
    KCWvwNavigationDelegate *navDelegate;
    char *script;
    NSString *nsScript;

    if (!ctx || !ctx->ns_webview || !ctx->bridge.callback) {
        return KC_WVW_OK;
    }

    script = kc_wvw_bridge_bootstrap_script(&ctx->bridge);
    if (!script) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        WKWebView *webView = (__bridge WKWebView *)ctx->ns_webview;
        manager = [webView configuration].userContentController;

        handler = [[KCWvwScriptMessageHandler alloc] init];
        handler.ctx = ctx;
        [manager addScriptMessageHandler:handler name:@"kc_wvw_native"];

        nsScript = [NSString stringWithUTF8String:script];
        WKUserScript *userScript = [[WKUserScript alloc]
            initWithSource:nsScript
            injectionTime:WKUserScriptInjectionTimeAtDocumentStart
            forMainFrameOnly:YES];
        [manager addUserScript:userScript];

        navDelegate = [[KCWvwNavigationDelegate alloc] init];
        navDelegate.ctx = ctx;
        [webView setNavigationDelegate:navDelegate];

        if (ctx->opts.borderless || ctx->opts.fullscreen) {
            [webView setValue:@(NO) forKey:@"drawsBackground"];
        }
    }

    free(script);
    return KC_WVW_OK;
}

int kc_wvw_enable_bridge(kc_wvw_t *ctx, const kc_wvw_bridge_options_t *opts) {
    kc_wvw_bridge_state_t bridge;

    if (!ctx || !opts) {
        return KC_WVW_ERROR;
    }

    memset(&bridge, 0, sizeof(bridge));
    if (kc_wvw_bridge_state_copy(&bridge, opts) != KC_WVW_OK) {
        return KC_WVW_ERROR;
    }
    if (!kc_wvw_bridge_url_trusted(ctx, &bridge, ctx->opts.url)) {
        kc_wvw_bridge_state_free(&bridge);
        return KC_WVW_ERROR;
    }

    kc_wvw_bridge_state_free(&ctx->bridge);
    ctx->bridge = bridge;
    return kc_wvw_macos_install_bridge(ctx);
}

int kc_wvw_post_bridge_event(kc_wvw_t *ctx, const char *json) {
    return kc_wvw_bridge_post_json(ctx, json);
}

static int kc_wvw_macos_create_window(kc_wvw_t *ctx) {
    @autoreleasepool {
        WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];
        WKWebView *webView = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:config];

        CGFloat r = 1.0, g = 1.0, b = 1.0, a = 1.0;
        int transparent = 0;

        if (ctx->opts.background) {
            if (kc_wvw_parse_background_color(ctx->opts.background, &r, &g, &b, &a) != KC_WVW_OK) {
                fprintf(stderr, "wvw: invalid background color '%s'\n", ctx->opts.background);
                return KC_WVW_ERROR;
            }
            transparent = kc_wvw_background_transparent(ctx->opts.background);
        }

        if (transparent) {
            [webView setValue:@(YES) forKey:@"drawsBackground"];
        } else {
            NSColor *bgColor = [NSColor colorWithCalibratedRed:r green:g blue:b alpha:a];
            [webView setValue:bgColor forKey:@"backgroundColor"];
        }

        NSString *urlStr = ctx->opts.url ? [NSString stringWithUTF8String:ctx->opts.url] : @"about:blank";
        NSURLRequest *request = [NSURLRequest requestWithURL:[NSURL URLWithString:urlStr]];
        [webView loadRequest:request];

        NSRect frame = NSMakeRect(0, 0,
            ctx->opts.width > 0 ? ctx->opts.width : 800,
            ctx->opts.height > 0 ? ctx->opts.height : 600);

        NSString *title = ctx->opts.title ? [NSString stringWithUTF8String:ctx->opts.title] : @"wvw";
        NSWindowStyleMask styleMask = NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

        if (!ctx->opts.borderless && !ctx->opts.fullscreen) {
            styleMask |= NSWindowStyleMaskTitled;
        }

        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:frame
            styleMask:styleMask
            backing:NSBackingStoreBuffered
            defer:NO];
        [window setTitle:title];

        if (transparent) {
            [window setBackgroundColor:[NSColor clearColor]];
            [window setOpaque:NO];
            [window setHasShadow:NO];
        }

        [window setContentView:webView];

        if (ctx->opts.fullscreen) {
            [window toggleFullScreen:nil];
        }
        if (ctx->opts.borderless) {
            [window setStyleMask:NSWindowStyleMaskBorderless];
        }
        if (ctx->opts.always_on_top) {
            [window setLevel:NSFloatingWindowLevel];
        }

        KCWvwWindowDelegate *windowDelegate = [[KCWvwWindowDelegate alloc] init];
        windowDelegate.ctx = ctx;
        [window setDelegate:windowDelegate];

        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        ctx->ns_window = (void *)CFBridgingRetain(window);
        ctx->ns_webview = (void *)CFBridgingRetain(webView);
        return KC_WVW_OK;
    }
}

int kc_wvw_show(kc_wvw_t *ctx) {
    if (!ctx || !ctx->ns_window) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        [window orderFront:nil];
    }
    return KC_WVW_OK;
}

int kc_wvw_hide(kc_wvw_t *ctx) {
    if (!ctx || !ctx->ns_window) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        [window orderOut:nil];
    }
    return KC_WVW_OK;
}

int kc_wvw_minimize(kc_wvw_t *ctx) {
    if (!ctx || !ctx->ns_window) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        [window miniaturize:nil];
    }
    return KC_WVW_OK;
}

int kc_wvw_maximize(kc_wvw_t *ctx) {
    if (!ctx || !ctx->ns_window) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        [window zoom:nil];
    }
    return KC_WVW_OK;
}

int kc_wvw_restore(kc_wvw_t *ctx) {
    if (!ctx || !ctx->ns_window) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        if ([window isMiniaturized]) {
            [window deminiaturize:nil];
        }
        if ([window isZoomed]) {
            [window zoom:nil];
        }
    }
    return KC_WVW_OK;
}

int kc_wvw_set_title(kc_wvw_t *ctx, const char *title) {
    if (!ctx || !ctx->ns_window || !title) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        NSString *nsTitle = [NSString stringWithUTF8String:title];
        [window setTitle:nsTitle];
    }
    return KC_WVW_OK;
}

int kc_wvw_set_size(kc_wvw_t *ctx, int width, int height) {
    if (!ctx || !ctx->ns_window || width <= 0 || height <= 0) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        NSRect frame = [window frame];
        NSSize contentSize = NSMakeSize((CGFloat)width, (CGFloat)height);
        [window setContentSize:contentSize];
        (void)frame;
    }
    return KC_WVW_OK;
}

int kc_wvw_get_state(kc_wvw_t *ctx, kc_wvw_window_state_t *state) {
    if (!ctx || !ctx->ns_window || !state) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        NSRect contentRect = [[window contentView] frame];
        memset(state, 0, sizeof(*state));
        state->width = (int)contentRect.size.width;
        state->height = (int)contentRect.size.height;
        state->minimized = [window isMiniaturized] ? 1 : 0;
        state->maximized = [window isZoomed] ? 1 : 0;
        state->fullscreen = [window styleMask] == NSWindowStyleMaskBorderless ? 1 : 0;
        state->visible = [window isVisible] ? 1 : 0;
    }
    return KC_WVW_OK;
}

static char *kc_wvw_bridge_param_string(const char *params, const char *key) {
    return kc_wvw_bridge_get_string(params, key);
}

static int kc_wvw_bridge_param_int(const char *params, const char *key, int *out) {
    char pattern[64];
    const char *start;
    char *end;
    long value;

    if (!params || !key || !out) {
        return KC_WVW_ERROR;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    start = strstr(params, pattern);
    if (!start) {
        return KC_WVW_ERROR;
    }

    start += strlen(pattern);
    while (*start == ' ') {
        start++;
    }

    value = strtol(start, &end, 10);
    if (end == start) {
        return KC_WVW_ERROR;
    }

    *out = (int)value;
    return KC_WVW_OK;
}

static void kc_wvw_macos_rebuild_tray_menu(kc_wvw_t *ctx) {
    NSMenu *menu;
    int i;

    if (!ctx || !ctx->ns_status_item) {
        return;
    }

    @autoreleasepool {
        NSStatusItem *statusItem = (__bridge NSStatusItem *)ctx->ns_status_item;
        menu = [[NSMenu alloc] init];

        if (ctx->tray_count > 0 && ctx->tray_items) {
            for (i = 0; i < ctx->tray_count; i++) {
                if (!ctx->tray_items[i].label) {
                    [menu addItem:[NSMenuItem separatorItem]];
                } else {
                    NSString *label = [NSString stringWithUTF8String:ctx->tray_items[i].label];
                    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:label action:@selector(kc_wvw_tray_item_action:) keyEquivalent:@""];
                    item.target = statusItem;
                    item.tag = i;
                    if (ctx->tray_items[i].action && strcmp(ctx->tray_items[i].action, "quit") == 0) {
                        item.action = @selector(kc_wvw_tray_quit_action:);
                    }
                    [menu addItem:item];
                }
            }
            [menu addItem:[NSMenuItem separatorItem]];
        }

        NSMenuItem *showHide = [[NSMenuItem alloc] initWithTitle:@"Show/Hide" action:@selector(kc_wvw_tray_toggle_action:) keyEquivalent:@""];
        [menu addItem:showHide];

        [menu addItem:[NSMenuItem separatorItem]];

        NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(kc_wvw_tray_quit_action:) keyEquivalent:@""];
        [menu addItem:quitItem];

        statusItem.menu = menu;
    }
}

@interface KCWvwStatusItemTarget : NSObject
@property (nonatomic, assign) kc_wvw_t *ctx;
@end

@implementation KCWvwStatusItemTarget
- (void)kc_wvw_tray_toggle_action:(id)sender {
    (void)sender;
    if (!self.ctx || !self.ctx->ns_window) {
        return;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)self.ctx->ns_window;
        if ([window isVisible]) {
            [window orderOut:nil];
        } else {
            [window makeKeyAndOrderFront:nil];
        }
    }
}

- (void)kc_wvw_tray_quit_action:(id)sender {
    (void)sender;
    if (!self.ctx) {
        return;
    }
    kc_wvw_tray_remove(self.ctx);
    kc_wvw_close(self.ctx);
}

- (void)kc_wvw_tray_item_action:(id)sender {
    if (!self.ctx || ![sender isKindOfClass:[NSMenuItem class]]) {
        return;
    }

    NSMenuItem *item = (NSMenuItem *)sender;
    NSInteger idx = item.tag;
    if (idx < 0 || idx >= self.ctx->tray_count) {
        return;
    }

    const char *action = self.ctx->tray_items[idx].action;
    if (action && strcmp(action, "quit") == 0) {
        kc_wvw_tray_remove(self.ctx);
        kc_wvw_close(self.ctx);
        return;
    }

    if (action) {
        char buf[256];
        int n = snprintf(buf, sizeof(buf),
            "{\"type\":\"tray_menu\",\"action\":\"%s\"}", action);
        if (n > 0 && n < (int)sizeof(buf)) {
            kc_wvw_post_bridge_event(self.ctx, buf);
        }
    }
}
@end

int kc_wvw_tray_init(kc_wvw_t *ctx, const char *tooltip, const char *icon) {
    if (!ctx) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSStatusItem *statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];

        if (icon) {
            NSString *iconStr = [NSString stringWithUTF8String:icon];
            NSImage *img = nil;
            if (strchr(icon, '/')) {
                img = [[NSImage alloc] initWithContentsOfFile:iconStr];
            } else {
                img = [NSImage imageNamed:iconStr];
            }
            if (img) {
                [img setSize:NSMakeSize(18, 18)];
                statusItem.button.image = img;
            }
        }
        if (!statusItem.button.image) {
            NSImage *defaultImg = [NSImage imageNamed:NSImageNameApplicationIcon];
            if (defaultImg) {
                [defaultImg setSize:NSMakeSize(18, 18)];
                statusItem.button.image = defaultImg;
            }
        }

        if (tooltip) {
            [[statusItem button] setToolTip:[NSString stringWithUTF8String:tooltip]];
        }

        KCWvwStatusItemTarget *target = [[KCWvwStatusItemTarget alloc] init];
        target.ctx = ctx;
        objc_setAssociatedObject(statusItem, "kc_wvw_target", target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        ctx->ns_status_item = (void *)CFBridgingRetain(statusItem);
        ctx->tray_enabled = 1;

        kc_wvw_macos_rebuild_tray_menu(ctx);
    }

    return KC_WVW_OK;
}

int kc_wvw_tray_remove(kc_wvw_t *ctx) {
    if (!ctx || !ctx->tray_enabled || !ctx->ns_status_item) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSStatusItem *statusItem = (__bridge NSStatusItem *)ctx->ns_status_item;
        [[NSStatusBar systemStatusBar] removeStatusItem:statusItem];
        CFRelease(ctx->ns_status_item);
        ctx->ns_status_item = NULL;
        ctx->tray_enabled = 0;
    }

    return KC_WVW_OK;
}

int kc_wvw_tray_set_menu(kc_wvw_t *ctx, const kc_wvw_tray_item_t *items, int count) {
    kc_wvw_tray_item_t *copy;
    int i;

    if (!ctx) {
        return KC_WVW_ERROR;
    }

    if (!items || count <= 0) {
        free(ctx->tray_items);
        ctx->tray_items = NULL;
        ctx->tray_count = 0;
        kc_wvw_macos_rebuild_tray_menu(ctx);
        return KC_WVW_OK;
    }

    copy = (kc_wvw_tray_item_t *)calloc((size_t)count, sizeof(kc_wvw_tray_item_t));
    if (!copy) {
        return KC_WVW_ERROR;
    }
    for (i = 0; i < count; i++) {
        copy[i].label = items[i].label ? kc_wvw_strdup(items[i].label) : NULL;
        copy[i].action = items[i].action ? kc_wvw_strdup(items[i].action) : NULL;
    }

    free(ctx->tray_items);
    ctx->tray_items = copy;
    ctx->tray_count = count;

    kc_wvw_macos_rebuild_tray_menu(ctx);
    return KC_WVW_OK;
}

int kc_wvw_open_window(kc_wvw_t *ctx) {
    if (!ctx) {
        return KC_WVW_ERROR;
    }
    return kc_wvw_macos_create_window(ctx);
}

int kc_wvw_run(kc_wvw_t *ctx) {
    if (!ctx) {
        return KC_WVW_ERROR;
    }

    ctx->running = 1;
    @autoreleasepool {
        [NSApp run];
    }
    return KC_WVW_OK;
}

int kc_wvw_close(kc_wvw_t *ctx) {
    if (!ctx) {
        return KC_WVW_OK;
    }

    if (ctx->tray_enabled) {
        kc_wvw_tray_remove(ctx);
    }

    if (ctx->ns_window) {
        @autoreleasepool {
            NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
            [window close];
        }
        CFRelease(ctx->ns_window);
        ctx->ns_window = NULL;
    }

    if (ctx->ns_webview) {
        @autoreleasepool {
            WKWebView *webView = (__bridge WKWebView *)ctx->ns_webview;
            [webView stopLoading];
        }
        CFRelease(ctx->ns_webview);
        ctx->ns_webview = NULL;
    }

    if (ctx->tray_items) {
        int i;
        for (i = 0; i < ctx->tray_count; i++) {
            free((void *)ctx->tray_items[i].label);
            free((void *)ctx->tray_items[i].action);
        }
        free(ctx->tray_items);
    }

    kc_wvw_bridge_state_free(&ctx->bridge);
    kc_wvw_options_free(&ctx->opts);
    free(ctx);
    return KC_WVW_OK;
}
