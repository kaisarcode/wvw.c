/**
 * libwvw.c - Core implementation for the wvw library.
 * Summary: Implements native WebView backends.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifdef _WIN32

#define CINTERFACE
#define COBJMACROS
#include "wvw.h"

#include <WebView2.h>
#include <shlobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#define KC_WVW_CLOSE_MESSAGE (WM_APP + 1)
#define KC_WVW_BRIDGE_MAX_MESSAGE 65536

typedef HRESULT (STDAPICALLTYPE *kc_wvw_create_environment_fn)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);
typedef HRESULT (STDAPICALLTYPE *kc_wvw_get_version_fn)(PCWSTR, LPWSTR *);

typedef enum {
    KC_WVW_INIT_PENDING,
    KC_WVW_INIT_READY,
    KC_WVW_INIT_FAILED,
} kc_wvw_init_state_t;

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
    kc_wvw_signal_entry_t *signal_handlers;
    int n_signal_handlers;
    int signal_handlers_capacity;
    int running;
    int closing;
    int com_initialized;
    HWND hwnd;
    HBRUSH background_brush;
    HINSTANCE hinstance;
    HMODULE loader;
    ICoreWebView2Environment *environment;
    ICoreWebView2Controller *controller;
    ICoreWebView2 *webview;
    kc_wvw_init_state_t init_state;
    char *pending_url;
    kc_wvw_bridge_state_t bridge;
};

static int kc_wvw_bridge_url_trusted(kc_wvw_bridge_state_t *bridge, const char *url);
static void kc_wvw_bridge_state_free(kc_wvw_bridge_state_t *bridge);
static int kc_wvw_bridge_state_copy(kc_wvw_bridge_state_t *dst, const kc_wvw_bridge_options_t *src);
static char *kc_wvw_bridge_bootstrap_script(kc_wvw_bridge_state_t *bridge, const char *sender_expr, const char *receiver_setup);
static int kc_wvw_bridge_post_json(kc_wvw_t *ctx, const char *json);
static char *kc_wvw_bridge_dispatch_request(kc_wvw_t *ctx, const char *json);

static const kc_env_map_t env_config_table[] = {
    { "KC_WVW_URL", offsetof(kc_wvw_options_t, url), KC_ENV_TYPE_STR },
    { "KC_WVW_TITLE", offsetof(kc_wvw_options_t, title), KC_ENV_TYPE_STR },
    { "KC_WVW_BACKGROUND", offsetof(kc_wvw_options_t, background), KC_ENV_TYPE_STR },
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

typedef struct kc_wvw_environment_handler {
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler iface;
    volatile LONG ref_count;
    kc_wvw_t *ctx;
} kc_wvw_environment_handler_t;

typedef struct kc_wvw_controller_handler {
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler iface;
    volatile LONG ref_count;
    kc_wvw_t *ctx;
} kc_wvw_controller_handler_t;

typedef struct kc_wvw_script_handler {
    ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler iface;
    volatile LONG ref_count;
} kc_wvw_script_handler_t;

typedef struct kc_wvw_navigation_handler {
    ICoreWebView2NavigationStartingEventHandler iface;
    volatile LONG ref_count;
    kc_wvw_t *ctx;
} kc_wvw_navigation_handler_t;

typedef struct kc_wvw_message_handler {
    ICoreWebView2WebMessageReceivedEventHandler iface;
    volatile LONG ref_count;
    kc_wvw_t *ctx;
} kc_wvw_message_handler_t;

static HRESULT STDMETHODCALLTYPE kc_wvw_environment_query_interface(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface, REFIID riid, void **object);
static ULONG STDMETHODCALLTYPE kc_wvw_environment_add_ref(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface);
static ULONG STDMETHODCALLTYPE kc_wvw_environment_release(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface);
static HRESULT STDMETHODCALLTYPE kc_wvw_environment_invoke(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface, HRESULT error_code, ICoreWebView2Environment *result);
static HRESULT STDMETHODCALLTYPE kc_wvw_controller_query_interface(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface, REFIID riid, void **object);
static ULONG STDMETHODCALLTYPE kc_wvw_controller_add_ref(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface);
static ULONG STDMETHODCALLTYPE kc_wvw_controller_release(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface);
static HRESULT STDMETHODCALLTYPE kc_wvw_controller_invoke(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface, HRESULT error_code, ICoreWebView2Controller *result);
static HRESULT STDMETHODCALLTYPE kc_wvw_script_query_interface(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface, REFIID riid, void **object);
static ULONG STDMETHODCALLTYPE kc_wvw_script_add_ref(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface);
static ULONG STDMETHODCALLTYPE kc_wvw_script_release(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface);
static HRESULT STDMETHODCALLTYPE kc_wvw_script_invoke(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface, HRESULT error_code, PCWSTR result);
static HRESULT STDMETHODCALLTYPE kc_wvw_navigation_query_interface(ICoreWebView2NavigationStartingEventHandler *iface, REFIID riid, void **object);
static ULONG STDMETHODCALLTYPE kc_wvw_navigation_add_ref(ICoreWebView2NavigationStartingEventHandler *iface);
static ULONG STDMETHODCALLTYPE kc_wvw_navigation_release(ICoreWebView2NavigationStartingEventHandler *iface);
static HRESULT STDMETHODCALLTYPE kc_wvw_navigation_invoke(ICoreWebView2NavigationStartingEventHandler *iface, ICoreWebView2 *sender, ICoreWebView2NavigationStartingEventArgs *args);
static HRESULT STDMETHODCALLTYPE kc_wvw_message_query_interface(ICoreWebView2WebMessageReceivedEventHandler *iface, REFIID riid, void **object);
static ULONG STDMETHODCALLTYPE kc_wvw_message_add_ref(ICoreWebView2WebMessageReceivedEventHandler *iface);
static ULONG STDMETHODCALLTYPE kc_wvw_message_release(ICoreWebView2WebMessageReceivedEventHandler *iface);
static HRESULT STDMETHODCALLTYPE kc_wvw_message_invoke(ICoreWebView2WebMessageReceivedEventHandler *iface, ICoreWebView2 *sender, ICoreWebView2WebMessageReceivedEventArgs *args);
static wchar_t *kc_wvw_utf16_from_utf8(const char *text);

static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl kc_wvw_environment_vtbl = {
    kc_wvw_environment_query_interface,
    kc_wvw_environment_add_ref,
    kc_wvw_environment_release,
    kc_wvw_environment_invoke
};

static ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl kc_wvw_controller_vtbl = {
    kc_wvw_controller_query_interface,
    kc_wvw_controller_add_ref,
    kc_wvw_controller_release,
    kc_wvw_controller_invoke
};

static ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandlerVtbl kc_wvw_script_vtbl = {
    kc_wvw_script_query_interface,
    kc_wvw_script_add_ref,
    kc_wvw_script_release,
    kc_wvw_script_invoke
};

static ICoreWebView2NavigationStartingEventHandlerVtbl kc_wvw_navigation_vtbl = {
    kc_wvw_navigation_query_interface,
    kc_wvw_navigation_add_ref,
    kc_wvw_navigation_release,
    kc_wvw_navigation_invoke
};

static ICoreWebView2WebMessageReceivedEventHandlerVtbl kc_wvw_message_vtbl = {
    kc_wvw_message_query_interface,
    kc_wvw_message_add_ref,
    kc_wvw_message_release,
    kc_wvw_message_invoke
};

/**
 * Detects whether the process is running under Wine.
 * @return Non-zero under Wine, otherwise zero.
 */
static int kc_wvw_is_wine(void) {
    HMODULE ntdll;

    ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version");
}

/**
 * Allocates a WebView2 environment completion handler.
 * @param ctx Window context.
 * @return Newly allocated handler or NULL.
 */
static kc_wvw_environment_handler_t *kc_wvw_environment_handler_new(kc_wvw_t *ctx) {
    kc_wvw_environment_handler_t *handler;

    handler = (kc_wvw_environment_handler_t *)calloc(1, sizeof(*handler));
    if (!handler) {
        return NULL;
    }

    handler->iface.lpVtbl = &kc_wvw_environment_vtbl;
    handler->ref_count = 1;
    handler->ctx = ctx;
    return handler;
}

/**
 * Allocates a WebView2 controller completion handler.
 * @param ctx Window context.
 * @return Newly allocated handler or NULL.
 */
static kc_wvw_controller_handler_t *kc_wvw_controller_handler_new(kc_wvw_t *ctx) {
    kc_wvw_controller_handler_t *handler;

    handler = (kc_wvw_controller_handler_t *)calloc(1, sizeof(*handler));
    if (!handler) {
        return NULL;
    }

    handler->iface.lpVtbl = &kc_wvw_controller_vtbl;
    handler->ref_count = 1;
    handler->ctx = ctx;
    return handler;
}

/**
 * Allocates one script completion handler.
 * @return Newly allocated handler or NULL.
 */
static kc_wvw_script_handler_t *kc_wvw_script_handler_new(void) {
    kc_wvw_script_handler_t *handler;

    handler = (kc_wvw_script_handler_t *)calloc(1, sizeof(*handler));
    if (!handler) {
        return NULL;
    }

    handler->iface.lpVtbl = &kc_wvw_script_vtbl;
    handler->ref_count = 1;
    return handler;
}

/**
 * Allocates one navigation event handler.
 * @param ctx Window context.
 * @return Newly allocated handler or NULL.
 */
static kc_wvw_navigation_handler_t *kc_wvw_navigation_handler_new(kc_wvw_t *ctx) {
    kc_wvw_navigation_handler_t *handler;

    handler = (kc_wvw_navigation_handler_t *)calloc(1, sizeof(*handler));
    if (!handler) {
        return NULL;
    }

    handler->iface.lpVtbl = &kc_wvw_navigation_vtbl;
    handler->ref_count = 1;
    handler->ctx = ctx;
    return handler;
}

/**
 * Allocates one web message event handler.
 * @param ctx Window context.
 * @return Newly allocated handler or NULL.
 */
static kc_wvw_message_handler_t *kc_wvw_message_handler_new(kc_wvw_t *ctx) {
    kc_wvw_message_handler_t *handler;

    handler = (kc_wvw_message_handler_t *)calloc(1, sizeof(*handler));
    if (!handler) {
        return NULL;
    }

    handler->iface.lpVtbl = &kc_wvw_message_vtbl;
    handler->ref_count = 1;
    handler->ctx = ctx;
    return handler;
}

/**
 * Returns a requested interface from the environment handler.
 * @param iface COM handler interface.
 * @param riid Requested interface identifier.
 * @param object Destination pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_environment_query_interface(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface, REFIID riid, void **object) {
    if (!object) {
        return E_POINTER;
    }

    *object = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
        *object = iface;
        kc_wvw_environment_add_ref(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

/**
 * Increments the environment handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_environment_add_ref(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface) {
    kc_wvw_environment_handler_t *handler = (kc_wvw_environment_handler_t *)iface;

    return (ULONG)InterlockedIncrement(&handler->ref_count);
}

/**
 * Decrements the environment handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_environment_release(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface) {
    kc_wvw_environment_handler_t *handler = (kc_wvw_environment_handler_t *)iface;
    ULONG next;

    next = (ULONG)InterlockedDecrement(&handler->ref_count);
    if (next == 0) {
        free(handler);
    }
    return next;
}

/**
 * Returns a requested interface from the controller handler.
 * @param iface COM handler interface.
 * @param riid Requested interface identifier.
 * @param object Destination pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_controller_query_interface(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface, REFIID riid, void **object) {
    if (!object) {
        return E_POINTER;
    }

    *object = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
        *object = iface;
        kc_wvw_controller_add_ref(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

/**
 * Increments the controller handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_controller_add_ref(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface) {
    kc_wvw_controller_handler_t *handler = (kc_wvw_controller_handler_t *)iface;

    return (ULONG)InterlockedIncrement(&handler->ref_count);
}

/**
 * Decrements the controller handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_controller_release(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface) {
    kc_wvw_controller_handler_t *handler = (kc_wvw_controller_handler_t *)iface;
    ULONG next;

    next = (ULONG)InterlockedDecrement(&handler->ref_count);
    if (next == 0) {
        free(handler);
    }
    return next;
}

/**
 * Returns a requested interface from the script handler.
 * @param iface COM handler interface.
 * @param riid Requested interface identifier.
 * @param object Destination pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_script_query_interface(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface, REFIID riid, void **object) {
    if (!object) {
        return E_POINTER;
    }

    *object = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler)) {
        *object = iface;
        kc_wvw_script_add_ref(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

/**
 * Increments the script handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_script_add_ref(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface) {
    kc_wvw_script_handler_t *handler = (kc_wvw_script_handler_t *)iface;

    return (ULONG)InterlockedIncrement(&handler->ref_count);
}

/**
 * Decrements the script handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_script_release(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface) {
    kc_wvw_script_handler_t *handler = (kc_wvw_script_handler_t *)iface;
    ULONG next;

    next = (ULONG)InterlockedDecrement(&handler->ref_count);
    if (next == 0) {
        free(handler);
    }
    return next;
}

/**
 * Completes one injected startup script registration.
 * @param iface COM handler interface.
 * @param error_code Completion status.
 * @param result Script identifier.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_script_invoke(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface, HRESULT error_code, PCWSTR result) {
    (void)iface;
    (void)error_code;
    (void)result;
    return S_OK;
}

/**
 * Returns a requested interface from the navigation handler.
 * @param iface COM handler interface.
 * @param riid Requested interface identifier.
 * @param object Destination pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_navigation_query_interface(ICoreWebView2NavigationStartingEventHandler *iface, REFIID riid, void **object) {
    if (!object) {
        return E_POINTER;
    }

    *object = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2NavigationStartingEventHandler)) {
        *object = iface;
        kc_wvw_navigation_add_ref(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

/**
 * Increments the navigation handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_navigation_add_ref(ICoreWebView2NavigationStartingEventHandler *iface) {
    kc_wvw_navigation_handler_t *handler = (kc_wvw_navigation_handler_t *)iface;

    return (ULONG)InterlockedIncrement(&handler->ref_count);
}

/**
 * Decrements the navigation handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_navigation_release(ICoreWebView2NavigationStartingEventHandler *iface) {
    kc_wvw_navigation_handler_t *handler = (kc_wvw_navigation_handler_t *)iface;
    ULONG next;

    next = (ULONG)InterlockedDecrement(&handler->ref_count);
    if (next == 0) {
        free(handler);
    }
    return next;
}

/**
 * Returns a requested interface from the message handler.
 * @param iface COM handler interface.
 * @param riid Requested interface identifier.
 * @param object Destination pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_message_query_interface(ICoreWebView2WebMessageReceivedEventHandler *iface, REFIID riid, void **object) {
    if (!object) {
        return E_POINTER;
    }

    *object = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2WebMessageReceivedEventHandler)) {
        *object = iface;
        kc_wvw_message_add_ref(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

/**
 * Increments the message handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_message_add_ref(ICoreWebView2WebMessageReceivedEventHandler *iface) {
    kc_wvw_message_handler_t *handler = (kc_wvw_message_handler_t *)iface;

    return (ULONG)InterlockedIncrement(&handler->ref_count);
}

/**
 * Decrements the message handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_message_release(ICoreWebView2WebMessageReceivedEventHandler *iface) {
    kc_wvw_message_handler_t *handler = (kc_wvw_message_handler_t *)iface;
    ULONG next;

    next = (ULONG)InterlockedDecrement(&handler->ref_count);
    if (next == 0) {
        free(handler);
    }
    return next;
}

/**
 * Returns the browser arguments for the WebView2 environment.
 * @return Argument string or NULL.
 */
static const char *kc_wvw_browser_args(void) {
    return getenv("KC_WVW_BROWSER_ARGS");
}

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

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} kc_wvw_text_buf_t;

/**
 * Initialize one text buffer.
 * @param buf Buffer state.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
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

/**
 * Append one byte range into the text buffer.
 * @param buf Buffer state.
 * @param text Source byte range.
 * @param len Source length.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
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

/**
 * Append one C string into the text buffer.
 * @param buf Buffer state.
 * @param text Source text.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_text_buf_append(kc_wvw_text_buf_t *buf, const char *text) {
    if (!text) {
        return KC_WVW_ERROR;
    }

    return kc_wvw_text_buf_append_n(buf, text, strlen(text));
}

/**
 * Duplicate one byte range into a C string.
 * @param start Start of the range.
 * @param len Range length.
 * @return Newly allocated string or NULL.
 */
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

/**
 * Return whether one bridge method name is safe for JS property injection.
 * @param method Candidate method name.
 * @return Non-zero when the identifier is accepted, otherwise zero.
 */
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

/**
 * Return whether one URL is trusted for the current bridge policy.
 * @param bridge Bridge state.
 * @param url Candidate URL.
 * @return Non-zero when the URL is accepted, otherwise zero.
 */
static int kc_wvw_bridge_url_trusted(kc_wvw_bridge_state_t *bridge, const char *url) {
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

    return 0;
}

/**
 * Release one copied bridge configuration.
 * @param bridge Bridge state.
 * @return None.
 */
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

/**
 * Copy one bridge configuration into the context state.
 * @param dst Destination bridge state.
 * @param src Source bridge options.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
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

/**
 * Return whether one method belongs to the current whitelist.
 * @param bridge Bridge state.
 * @param method Candidate method name.
 * @return Non-zero when the method is accepted, otherwise zero.
 */
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

/**
 * Extract one string field from a bridge request.
 * @param json Source JSON payload.
 * @param key Field name.
 * @return Newly allocated string value or NULL.
 */
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

/**
 * Extract the params payload from one bridge request.
 * @param json Source JSON payload.
 * @return Newly allocated JSON fragment or NULL.
 */
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

/**
 * Build one bridge response payload.
 * @param id Request identifier.
 * @param ok Success flag.
 * @param body Serialized result or error object.
 * @return Newly allocated response payload or NULL.
 */
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

/**
 * Build one JSON error object.
 * @param code Error code string.
 * @param message Error message string.
 * @return Newly allocated JSON error object or NULL.
 */
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

/**
 * Build the injected NativeBridge bootstrap script.
 * @param bridge Bridge state.
 * @param sender_expr JavaScript sender expression.
 * @param receiver_setup JavaScript receiver setup.
 * @return Newly allocated script text or NULL.
 */
static char *kc_wvw_bridge_bootstrap_script(kc_wvw_bridge_state_t *bridge, const char *sender_expr, const char *receiver_setup) {
    kc_wvw_text_buf_t buf;
    int i;

    if (!bridge || !sender_expr || !receiver_setup || kc_wvw_text_buf_init(&buf) != KC_WVW_OK) {
        return NULL;
    }

    if (kc_wvw_text_buf_append(&buf, "(function(){if(window.NativeBridge){return;}var __kcWvwPending={};var __kcWvwSeq=0;function __kcWvwReceive(msg){if(msg&&typeof msg.id==='string'){var cb=__kcWvwPending[msg.id];if(cb){delete __kcWvwPending[msg.id];cb(msg.ok?null:(msg.error||{code:'ERROR',message:'Bridge error'}),msg.ok?msg.result:null);}return;}window.dispatchEvent(new CustomEvent('") != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, KC_WVW_BRIDGE_EVENT_NAME) != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, "',{detail:msg}));}window.__kcWvwReceive=__kcWvwReceive;window.NativeBridge={};function __kcWvwSend(method,params,callback){var id=String(++__kcWvwSeq);if(typeof callback==='function'){__kcWvwPending[id]=callback;}(") != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, sender_expr) != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, ")(JSON.stringify({id:id,method:method,params:params===undefined?null:params}));}") != KC_WVW_OK) {
        free(buf.data);
        return NULL;
    }

    for (i = 0; i < bridge->method_count; i++) {
        if (kc_wvw_text_buf_append(&buf, "window.NativeBridge.") != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, bridge->methods[i]) != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, "=function(params,callback){return __kcWvwSend('") != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, bridge->methods[i]) != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, "',params,callback);};") != KC_WVW_OK) {
            free(buf.data);
            return NULL;
        }
    }

    if (kc_wvw_text_buf_append(&buf, receiver_setup) != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, "}());") != KC_WVW_OK) {
        free(buf.data);
        return NULL;
    }

    return buf.data;
}

/**
 * Deliver one JSON payload into the WebView bridge runtime.
 * @param ctx Window context.
 * @param json JSON payload.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_bridge_post_json(kc_wvw_t *ctx, const char *json) {
    wchar_t *wide;
    HRESULT hr;

    if (!ctx || !ctx->webview || !json || strlen(json) > KC_WVW_BRIDGE_MAX_MESSAGE) {
        return KC_WVW_ERROR;
    }

    wide = kc_wvw_utf16_from_utf8(json);
    if (!wide) {
        return KC_WVW_ERROR;
    }

    hr = ICoreWebView2_PostWebMessageAsJson(ctx->webview, wide);
    free(wide);
    return FAILED(hr) ? KC_WVW_ERROR : KC_WVW_OK;
}

/**
 * Dispatch one bridge request into the application callback.
 * @param ctx Window context.
 * @param json Request payload.
 * @return Newly allocated response payload or NULL.
 */
static char *kc_wvw_bridge_dispatch_request(kc_wvw_t *ctx, const char *json) {
    char *id;
    char *method;
    char *params;
    char *result;
    char *error;
    char *response;
    int rc;

    if (!ctx || !json || strlen(json) > KC_WVW_BRIDGE_MAX_MESSAGE) {
        error = kc_wvw_bridge_error_object("BAD_REQUEST", "Bridge request is invalid.");
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
        error = kc_wvw_bridge_error_object("BAD_REQUEST", "Bridge request is malformed.");
        response = error ? kc_wvw_bridge_wrap_response("0", 0, error) : NULL;
        free(error);
        return response;
    }

    if (!kc_wvw_bridge_method_allowed(&ctx->bridge, method)) {
        error = kc_wvw_bridge_error_object("METHOD_NOT_ALLOWED", "Bridge method is not allowed.");
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
            result = kc_wvw_bridge_error_object("CALL_FAILED", "Bridge callback failed.");
        }
        response = result ? kc_wvw_bridge_wrap_response(id, 0, result) : NULL;
    }

    free(id);
    free(method);
    free(params);
    free(result);
    return response;
}

/**
 * Decide whether one navigation request stays inside the trusted WebView.
 * @param iface COM handler interface.
 * @param sender WebView sender.
 * @param args Navigation event args.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_navigation_invoke(ICoreWebView2NavigationStartingEventHandler *iface, ICoreWebView2 *sender, ICoreWebView2NavigationStartingEventArgs *args) {
    kc_wvw_navigation_handler_t *handler = (kc_wvw_navigation_handler_t *)iface;
    LPWSTR uri = NULL;
    char utf8[4096];

    (void)sender;
    if (!handler || !handler->ctx || !handler->ctx->bridge.callback || !args) {
        return S_OK;
    }

    if (SUCCEEDED(ICoreWebView2NavigationStartingEventArgs_get_Uri(args, &uri)) && uri) {
        utf8[0] = '\0';
        WideCharToMultiByte(CP_UTF8, 0, uri, -1, utf8, sizeof(utf8), NULL, NULL);
        if (!kc_wvw_bridge_url_trusted(&handler->ctx->bridge, utf8)) {
            ICoreWebView2NavigationStartingEventArgs_put_Cancel(args, TRUE);
        }
        CoTaskMemFree(uri);
    }

    return S_OK;
}

/**
 * Dispatch one WebView message into the current bridge callback.
 * @param iface COM handler interface.
 * @param sender WebView sender.
 * @param args Message event args.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_message_invoke(ICoreWebView2WebMessageReceivedEventHandler *iface, ICoreWebView2 *sender, ICoreWebView2WebMessageReceivedEventArgs *args) {
    kc_wvw_message_handler_t *handler = (kc_wvw_message_handler_t *)iface;
    LPWSTR message = NULL;
    char utf8[KC_WVW_BRIDGE_MAX_MESSAGE + 1];
    char *response;

    (void)sender;
    if (!handler || !handler->ctx || !args) {
        return S_OK;
    }

    if (FAILED(ICoreWebView2WebMessageReceivedEventArgs_TryGetWebMessageAsString(args, &message)) || !message) {
        return S_OK;
    }

    utf8[0] = '\0';
    WideCharToMultiByte(CP_UTF8, 0, message, -1, utf8, sizeof(utf8), NULL, NULL);
    response = kc_wvw_bridge_dispatch_request(handler->ctx, utf8);
    if (response) {
        kc_wvw_bridge_post_json(handler->ctx, response);
        free(response);
    }
    CoTaskMemFree(message);
    return S_OK;
}

/**
 * Parses one hexadecimal WebView background color.
 * @param text Color text in RRGGBB or AARRGGBB format.
 * @param out_a Destination alpha byte.
 * @param out_r Destination red byte.
 * @param out_g Destination green byte.
 * @param out_b Destination blue byte.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_parse_background_color(const char *text, BYTE *out_a, BYTE *out_r, BYTE *out_g, BYTE *out_b) {
    size_t length;
    unsigned int parts[4];

    if (!text || !out_a || !out_r || !out_g || !out_b) {
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

    *out_a = (BYTE)parts[0];
    *out_r = (BYTE)parts[1];
    *out_g = (BYTE)parts[2];
    *out_b = (BYTE)parts[3];
    return KC_WVW_OK;
}

/**
 * Formats one parsed background color for the WebView2 environment variable.
 * @param text Color text in RRGGBB or AARRGGBB format.
 * @return Newly allocated AARRGGBB string or NULL.
 */
static char *kc_wvw_background_hex8(const char *text) {
    BYTE a;
    BYTE r;
    BYTE g;
    BYTE b;
    char *color;

    if (kc_wvw_parse_background_color(text, &a, &r, &g, &b) != KC_WVW_OK) {
        return NULL;
    }
    if (a != 0x00 && a != 0xff) {
        return NULL;
    }

    color = (char *)malloc(9);
    if (!color) {
        return NULL;
    }

    snprintf(color, 9, "%02X%02X%02X%02X", (unsigned int)a, (unsigned int)r, (unsigned int)g, (unsigned int)b);
    return color;
}

/**
 * Creates one native window background brush from the configured color.
 * @param text Color text in RRGGBB or AARRGGBB format.
 * @return Newly allocated brush or NULL.
 */
static HBRUSH kc_wvw_background_brush(const char *text) {
    BYTE a;
    BYTE r;
    BYTE g;
    BYTE b;

    if (!text || kc_wvw_parse_background_color(text, &a, &r, &g, &b) != KC_WVW_OK) {
        return NULL;
    }

    return CreateSolidBrush(RGB(r, g, b));
}

/**
 * Converts UTF-8 text to UTF-16 text.
 * @param text UTF-8 source text.
 * @return Newly allocated UTF-16 text or NULL.
 */
static wchar_t *kc_wvw_utf16_from_utf8(const char *text) {
    int length;
    wchar_t *wide;

    if (!text) {
        return NULL;
    }

    length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (length <= 0) {
        return NULL;
    }

    wide = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
    if (!wide) {
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, length) <= 0) {
        free(wide);
        return NULL;
    }

    return wide;
}

/**
 * Joins two UTF-16 path segments.
 * @param left Left path segment.
 * @param right Right path segment.
 * @return Newly allocated path or NULL.
 */
static wchar_t *kc_wvw_join_path(const wchar_t *left, const wchar_t *right) {
    size_t left_len;
    size_t right_len;
    size_t slash;
    wchar_t *path;

    if (!left || !right) {
        return NULL;
    }

    left_len = wcslen(left);
    right_len = wcslen(right);
    slash = left_len > 0 && left[left_len - 1] != L'\\' ? 1 : 0;
    path = (wchar_t *)malloc((left_len + slash + right_len + 1) * sizeof(wchar_t));
    if (!path) {
        return NULL;
    }

    wcscpy(path, left);
    if (slash) {
        wcscat(path, L"\\");
    }
    wcscat(path, right);
    return path;
}

/**
 * Returns the executable directory.
 * @return Newly allocated directory path or NULL.
 */
static wchar_t *kc_wvw_executable_dir(void) {
    DWORD length;
    wchar_t *slash;
    wchar_t buffer[MAX_PATH];

    length = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return NULL;
    }

    slash = wcsrchr(buffer, L'\\');
    if (slash) {
        *slash = L'\0';
    }
    return _wcsdup(buffer);
}

/**
 * Loads WebView2Loader.dll from beside the executable.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_load_loader(kc_wvw_t *ctx) {
    wchar_t *dir;
    wchar_t *dll_path;

    dir = kc_wvw_executable_dir();
    if (!dir) {
        return KC_WVW_ERROR;
    }

    dll_path = kc_wvw_join_path(dir, L"WebView2Loader.dll");
    free(dir);
    if (!dll_path) {
        return KC_WVW_ERROR;
    }

    ctx->loader = LoadLibraryW(dll_path);
    free(dll_path);
    if (!ctx->loader) {
        fprintf(stderr, "wvw: WebView2Loader.dll must be beside wvw.exe\n");
        return KC_WVW_ERROR;
    }

    return KC_WVW_OK;
}

/**
 * Creates the persistent WebView2 user-data directory.
 * @return Newly allocated directory path or NULL.
 */
static wchar_t *kc_wvw_user_data_dir(void) {
    PWSTR local_app_data = NULL;
    wchar_t *kaisar_dir;
    wchar_t *wvw_dir;
    wchar_t *webview_dir;

    if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &local_app_data))) {
        return NULL;
    }

    kaisar_dir = kc_wvw_join_path(local_app_data, L"KaisarCode");
    CoTaskMemFree(local_app_data);
    if (!kaisar_dir) {
        return NULL;
    }

    wvw_dir = kc_wvw_join_path(kaisar_dir, L"wvw");
    if (!wvw_dir) {
        free(kaisar_dir);
        return NULL;
    }

    webview_dir = kc_wvw_join_path(wvw_dir, L"WebView2");
    if (!webview_dir) {
        free(kaisar_dir);
        free(wvw_dir);
        return NULL;
    }

    CreateDirectoryW(kaisar_dir, NULL);
    CreateDirectoryW(wvw_dir, NULL);
    CreateDirectoryW(webview_dir, NULL);
    free(kaisar_dir);
    free(wvw_dir);
    return webview_dir;
}

/**
 * Updates the WebView2 controller to fill the client area.
 * @param ctx Window context.
 * @return None.
 */
static void kc_wvw_update_bounds(kc_wvw_t *ctx) {
    RECT bounds;

    if (!ctx || !ctx->controller || !ctx->hwnd) {
        return;
    }

    GetClientRect(ctx->hwnd, &bounds);
    ICoreWebView2Controller_put_Bounds(ctx->controller, bounds);
}

/**
 * Applies application-surface WebView settings.
 * @param ctx Window context.
 * @return None.
 */
static void kc_wvw_apply_settings(kc_wvw_t *ctx) {
    ICoreWebView2Settings *settings = NULL;
    ICoreWebView2Settings3 *settings3 = NULL;

    if (!ctx || !ctx->webview) {
        return;
    }

    if (FAILED(ICoreWebView2_get_Settings(ctx->webview, &settings)) || !settings) {
        return;
    }

    ICoreWebView2Settings_put_IsStatusBarEnabled(settings, FALSE);
    ICoreWebView2Settings_put_IsZoomControlEnabled(settings, FALSE);
#ifdef NDEBUG
    ICoreWebView2Settings_put_AreDevToolsEnabled(settings, FALSE);
    ICoreWebView2Settings_put_AreDefaultContextMenusEnabled(settings, FALSE);
#endif
    if (SUCCEEDED(ICoreWebView2Settings_QueryInterface(settings, &IID_ICoreWebView2Settings3, (void **)&settings3)) && settings3) {
        ICoreWebView2Settings3_put_AreBrowserAcceleratorKeysEnabled(settings3, FALSE);
        ICoreWebView2Settings3_Release(settings3);
    }
    ICoreWebView2Settings_Release(settings);
}

/**
 * Applies one configured default background color to WebView2.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_apply_background_color(kc_wvw_t *ctx) {
    ICoreWebView2Controller2 *controller2 = NULL;
    COREWEBVIEW2_COLOR color;
    HRESULT hr;

    if (!ctx || !ctx->controller || !ctx->opts.background) {
        return KC_WVW_OK;
    }
    if (kc_wvw_parse_background_color(ctx->opts.background, &color.A, &color.R, &color.G, &color.B) != KC_WVW_OK) {
        fprintf(stderr, "wvw: invalid background color '%s'\n", ctx->opts.background);
        return KC_WVW_ERROR;
    }
    if (color.A != 0x00 && color.A != 0xff) {
        fprintf(stderr, "wvw: Windows background alpha must be 00 or FF\n");
        return KC_WVW_ERROR;
    }

    hr = ICoreWebView2Controller_QueryInterface(ctx->controller, &IID_ICoreWebView2Controller2, (void **)&controller2);
    if (FAILED(hr) || !controller2) {
        return KC_WVW_OK;
    }

    hr = ICoreWebView2Controller2_put_DefaultBackgroundColor(controller2, color);
    ICoreWebView2Controller2_Release(controller2);
    return FAILED(hr) ? KC_WVW_ERROR : KC_WVW_OK;
}

/**
 * Install the configured bridge into the current WebView.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_windows_install_bridge(kc_wvw_t *ctx) {
    kc_wvw_script_handler_t *script_handler;
    kc_wvw_navigation_handler_t *navigation_handler;
    kc_wvw_message_handler_t *message_handler;
    EventRegistrationToken token;
    wchar_t *script_wide;
    char *script;
    HRESULT hr;

    if (!ctx || !ctx->webview || !ctx->bridge.callback) {
        return KC_WVW_OK;
    }

    script = kc_wvw_bridge_bootstrap_script(&ctx->bridge, "window.chrome.webview.postMessage", "window.chrome.webview.addEventListener('message',function(e){__kcWvwReceive(e.data);});");
    if (!script) {
        return KC_WVW_ERROR;
    }

    script_wide = kc_wvw_utf16_from_utf8(script);
    free(script);
    if (!script_wide) {
        return KC_WVW_ERROR;
    }

    script_handler = kc_wvw_script_handler_new();
    if (!script_handler) {
        free(script_wide);
        return KC_WVW_ERROR;
    }

    hr = ICoreWebView2_AddScriptToExecuteOnDocumentCreated(ctx->webview, script_wide, (ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *)script_handler);
    ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler_Release((ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *)script_handler);
    free(script_wide);
    if (FAILED(hr)) {
        return KC_WVW_ERROR;
    }

    navigation_handler = kc_wvw_navigation_handler_new(ctx);
    if (!navigation_handler) {
        return KC_WVW_ERROR;
    }
    hr = ICoreWebView2_add_NavigationStarting(ctx->webview, (ICoreWebView2NavigationStartingEventHandler *)navigation_handler, &token);
    ICoreWebView2NavigationStartingEventHandler_Release((ICoreWebView2NavigationStartingEventHandler *)navigation_handler);
    if (FAILED(hr)) {
        return KC_WVW_ERROR;
    }

    message_handler = kc_wvw_message_handler_new(ctx);
    if (!message_handler) {
        return KC_WVW_ERROR;
    }
    hr = ICoreWebView2_add_WebMessageReceived(ctx->webview, (ICoreWebView2WebMessageReceivedEventHandler *)message_handler, &token);
    ICoreWebView2WebMessageReceivedEventHandler_Release((ICoreWebView2WebMessageReceivedEventHandler *)message_handler);
    if (FAILED(hr)) {
        return KC_WVW_ERROR;
    }

    return KC_WVW_OK;
}

/**
 * Navigates to the pending URL when the WebView is ready.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_flush_pending_navigation(kc_wvw_t *ctx) {
    wchar_t *target;
    HRESULT hr;

    if (!ctx || !ctx->webview || !ctx->pending_url) {
        return KC_WVW_OK;
    }

    target = kc_wvw_utf16_from_utf8(ctx->pending_url);
    if (!target) {
        return KC_WVW_ERROR;
    }

    hr = ICoreWebView2_Navigate(ctx->webview, target);
    free(target);
    return FAILED(hr) ? KC_WVW_ERROR : KC_WVW_OK;
}

/**
 * Performs UI-thread shutdown for the host window.
 * @param ctx Window context.
 * @return None.
 */
static void kc_wvw_request_close(kc_wvw_t *ctx) {
    if (!ctx || ctx->closing) {
        return;
    }

    ctx->closing = 1;
    if (ctx->hwnd && IsWindow(ctx->hwnd)) {
        DestroyWindow(ctx->hwnd);
    } else {
        ctx->running = 0;
    }
}

/**
 * Processes messages while WebView2 initializes asynchronously.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_wait_for_ready(kc_wvw_t *ctx) {
    MSG message;

    while (ctx->init_state == KC_WVW_INIT_PENDING && !ctx->closing) {
        BOOL rc = GetMessageW(&message, NULL, 0, 0);
        if (rc <= 0) {
            ctx->init_state = KC_WVW_INIT_FAILED;
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return ctx->init_state == KC_WVW_INIT_READY ? KC_WVW_OK : KC_WVW_ERROR;
}

/**
 * Handles native Win32 window messages.
 * @param hwnd Window handle.
 * @param msg Message identifier.
 * @param wparam Message parameter.
 * @param lparam Message parameter.
 * @return Window procedure result.
 */
static LRESULT CALLBACK kc_wvw_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    kc_wvw_t *ctx = (kc_wvw_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_SIZE:
        kc_wvw_update_bounds(ctx);
        return 0;
    case WM_ERASEBKGND:
        if (ctx && ctx->background_brush) {
            RECT rect;
            HDC dc = (HDC)wparam;
            GetClientRect(hwnd, &rect);
            FillRect(dc, &rect, ctx->background_brush);
            return 1;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_SETFOCUS:
        if (ctx && ctx->controller) {
            ICoreWebView2Controller_MoveFocus(ctx->controller, COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
        return 0;
    case WM_SETCURSOR:
        if (kc_wvw_is_wine()) {
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_CLOSE:
        kc_wvw_request_close(ctx);
        return 0;
    case KC_WVW_CLOSE_MESSAGE:
        kc_wvw_request_close(ctx);
        return 0;
    case WM_DESTROY:
        if (ctx) {
            ctx->running = 0;
            ctx->hwnd = NULL;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

/**
 * Creates the native Win32 host window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_create_window(kc_wvw_t *ctx) {
    WNDCLASSW klass;
    DWORD style;
    DWORD ex_style;
    RECT rect;
    wchar_t *title;
    const wchar_t class_name[] = L"kc_wvw_window";

    memset(&klass, 0, sizeof(klass));
    klass.lpfnWndProc = kc_wvw_window_proc;
    klass.hInstance = ctx->hinstance;
    klass.lpszClassName = class_name;
    klass.hCursor = LoadCursor(NULL, IDC_ARROW);
    klass.hbrBackground = NULL;
    RegisterClassW(&klass);

    style = WS_OVERLAPPEDWINDOW;
    ex_style = 0;
    if (ctx->opts.borderless || ctx->opts.fullscreen) {
        style = WS_POPUP | WS_VISIBLE;
    }

    rect.left = 0;
    rect.top = 0;
    rect.right = ctx->opts.width;
    rect.bottom = ctx->opts.height;
    AdjustWindowRectEx(&rect, style, FALSE, ex_style);

    if (ctx->opts.fullscreen) {
        rect.left = 0;
        rect.top = 0;
        rect.right = GetSystemMetrics(SM_CXSCREEN);
        rect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    title = kc_wvw_utf16_from_utf8(ctx->opts.title ? ctx->opts.title : "wvw");
    ctx->hwnd = CreateWindowExW(
        ex_style,
        class_name,
        title ? title : L"wvw",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        NULL,
        NULL,
        ctx->hinstance,
        ctx
    );
    free(title);

    if (!ctx->hwnd) {
        return KC_WVW_ERROR;
    }

    ShowWindow(ctx->hwnd, SW_SHOWDEFAULT);
    UpdateWindow(ctx->hwnd);
    return KC_WVW_OK;
}

/**
 * Starts asynchronous WebView2 initialization.
 * @param ctx Window context.
 * @return KC_WVW_OK on start or KC_WVW_ERROR on failure.
 */
static int kc_wvw_start_webview(kc_wvw_t *ctx) {
    kc_wvw_create_environment_fn create_environment;
    kc_wvw_get_version_fn get_version;
    FARPROC create_environment_proc;
    FARPROC get_version_proc;
    kc_wvw_environment_handler_t *handler;
    wchar_t *user_data_dir;
    const char *browser_args;
    char *background;
    LPWSTR version = NULL;
    HRESULT hr;

    create_environment_proc = GetProcAddress(ctx->loader, "CreateCoreWebView2EnvironmentWithOptions");
    get_version_proc = GetProcAddress(ctx->loader, "GetAvailableCoreWebView2BrowserVersionString");
    memcpy(&create_environment, &create_environment_proc, sizeof(create_environment));
    memcpy(&get_version, &get_version_proc, sizeof(get_version));
    if (!create_environment || !get_version) {
        fprintf(stderr, "wvw: invalid WebView2Loader.dll\n");
        return KC_WVW_ERROR;
    }

    hr = get_version(NULL, &version);
    if (FAILED(hr) || !version) {
        fprintf(stderr, "wvw: Microsoft Edge WebView2 Runtime is required\n");
        return KC_WVW_ERROR;
    }
    CoTaskMemFree(version);

    user_data_dir = kc_wvw_user_data_dir();
    if (!user_data_dir) {
        return KC_WVW_ERROR;
    }

    browser_args = kc_wvw_browser_args();
    if (browser_args && *browser_args && !getenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS")) {
        SetEnvironmentVariableA("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", browser_args);
    }
    background = NULL;
    if (ctx->opts.background && *ctx->opts.background) {
        background = kc_wvw_background_hex8(ctx->opts.background);
        if (!background) {
            fprintf(stderr, "wvw: invalid background color '%s'\n", ctx->opts.background);
            free(user_data_dir);
            return KC_WVW_ERROR;
        }
        SetEnvironmentVariableA("WEBVIEW2_DEFAULT_BACKGROUND_COLOR", background);
        free(background);
    }

    handler = kc_wvw_environment_handler_new(ctx);
    if (!handler) {
        free(user_data_dir);
        return KC_WVW_ERROR;
    }
    hr = create_environment(NULL, user_data_dir, NULL, (ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *)handler);
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler_Release((ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *)handler);
    free(user_data_dir);
    if (FAILED(hr)) {
        return KC_WVW_ERROR;
    }

    return KC_WVW_OK;
}

/**
 * Receives the created WebView2 environment.
 * @param error_code Completion status.
 * @param result Created environment.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_environment_invoke(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface, HRESULT error_code, ICoreWebView2Environment *result) {
    kc_wvw_environment_handler_t *env_handler = (kc_wvw_environment_handler_t *)iface;
    kc_wvw_controller_handler_t *handler;
    HRESULT hr;
    kc_wvw_t *ctx = env_handler->ctx;

    if (!ctx || ctx->closing || FAILED(error_code) || !result) {
        if (ctx) {
            ctx->init_state = KC_WVW_INIT_FAILED;
        }
        return S_OK;
    }

    ctx->environment = result;
    ICoreWebView2Environment_AddRef(ctx->environment);
    handler = kc_wvw_controller_handler_new(ctx);
    if (!handler) {
        ctx->init_state = KC_WVW_INIT_FAILED;
        return S_OK;
    }
    hr = ICoreWebView2Environment_CreateCoreWebView2Controller(ctx->environment, ctx->hwnd, (ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *)handler);
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler_Release((ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *)handler);
    if (FAILED(hr)) {
        ctx->init_state = KC_WVW_INIT_FAILED;
    }
    return S_OK;
}

/**
 * Receives the created WebView2 controller.
 * @param error_code Completion status.
 * @param result Created controller.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_controller_invoke(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface, HRESULT error_code, ICoreWebView2Controller *result) {
    kc_wvw_controller_handler_t *handler = (kc_wvw_controller_handler_t *)iface;
    kc_wvw_t *ctx = handler->ctx;

    if (!ctx || ctx->closing || FAILED(error_code) || !result) {
        if (ctx) {
            ctx->init_state = KC_WVW_INIT_FAILED;
        }
        return S_OK;
    }

    ctx->controller = result;
    ICoreWebView2Controller_AddRef(ctx->controller);
    if (FAILED(ICoreWebView2Controller_get_CoreWebView2(ctx->controller, &ctx->webview)) || !ctx->webview) {
        ctx->init_state = KC_WVW_INIT_FAILED;
        return S_OK;
    }

    kc_wvw_update_bounds(ctx);
    if (kc_wvw_apply_background_color(ctx) != KC_WVW_OK) {
        ctx->init_state = KC_WVW_INIT_FAILED;
        return S_OK;
    }
    kc_wvw_apply_settings(ctx);
    if (kc_wvw_windows_install_bridge(ctx) != KC_WVW_OK) {
        ctx->init_state = KC_WVW_INIT_FAILED;
        return S_OK;
    }
    if (kc_wvw_flush_pending_navigation(ctx) != KC_WVW_OK) {
        ctx->init_state = KC_WVW_INIT_FAILED;
        return S_OK;
    }

    ctx->init_state = KC_WVW_INIT_READY;
    return S_OK;
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
    free(opts->background);
    opts->url = NULL;
    opts->title = NULL;
    opts->background = NULL;
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
    (void)sig_id;
    return KC_WVW_OK;
}

/**
 * Dispatch an operating-system signal into the library signal table.
 * @param sig Operating-system signal number.
 * @return None.
 */
void kc_wvw_signal_listener(int sig) {
    if (g_signal_ctx) {
        kc_wvw_raise_signal(g_signal_ctx, sig);
    }
}

/**
 * Create a new native WebView context.
 * @param ctx_out Destination context pointer.
 * @param opts Configuration options.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_open(kc_wvw_t **ctx_out, kc_wvw_options_t *opts) {
    kc_wvw_t *ctx;
    HRESULT hr;

    if (!ctx_out || !opts || !opts->url) {
        return KC_WVW_ERROR;
    }

    ctx = (kc_wvw_t *)calloc(1, sizeof(kc_wvw_t));
    if (!ctx) {
        return KC_WVW_ERROR;
    }

    ctx->init_state = KC_WVW_INIT_PENDING;
    ctx->opts = *opts;
    ctx->opts.url = kc_wvw_strdup(opts->url);
    ctx->opts.title = opts->title ? kc_wvw_strdup(opts->title) : kc_wvw_strdup("wvw");
    ctx->opts.background = opts->background ? kc_wvw_strdup(opts->background) : NULL;
    ctx->opts.width = opts->width;
    ctx->opts.height = opts->height;
    ctx->opts.fullscreen = opts->fullscreen;
    ctx->opts.borderless = opts->borderless;
    ctx->pending_url = kc_wvw_strdup(opts->url);
    if (!ctx->opts.url || !ctx->opts.title || (opts->background && !ctx->opts.background) || !ctx->pending_url) {
        kc_wvw_close(ctx);
        return KC_WVW_ERROR;
    }

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        kc_wvw_close(ctx);
        return KC_WVW_ERROR;
    }
    ctx->com_initialized = SUCCEEDED(hr);
    ctx->hinstance = GetModuleHandleW(NULL);
    ctx->background_brush = kc_wvw_background_brush(ctx->opts.background);

    if (kc_wvw_load_loader(ctx) != KC_WVW_OK || kc_wvw_create_window(ctx) != KC_WVW_OK || kc_wvw_start_webview(ctx) != KC_WVW_OK || kc_wvw_wait_for_ready(ctx) != KC_WVW_OK) {
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

    ctx->closing = 1;
    if (ctx->controller) {
        ICoreWebView2Controller_Close(ctx->controller);
    }
    if (ctx->webview) {
        ICoreWebView2_Release(ctx->webview);
        ctx->webview = NULL;
    }
    if (ctx->controller) {
        ICoreWebView2Controller_Release(ctx->controller);
        ctx->controller = NULL;
    }
    if (ctx->environment) {
        ICoreWebView2Environment_Release(ctx->environment);
        ctx->environment = NULL;
    }
    if (ctx->hwnd && IsWindow(ctx->hwnd)) {
        DestroyWindow(ctx->hwnd);
        ctx->hwnd = NULL;
    }
    if (ctx->loader) {
        FreeLibrary(ctx->loader);
        ctx->loader = NULL;
    }
    if (ctx->background_brush) {
        DeleteObject(ctx->background_brush);
        ctx->background_brush = NULL;
    }

    free(ctx->pending_url);
    free(ctx->signal_handlers);
    kc_wvw_bridge_state_free(&ctx->bridge);
    kc_wvw_options_free(&ctx->opts);
    if (ctx->com_initialized) {
        CoUninitialize();
    }
    free(ctx);
    return KC_WVW_OK;
}

/**
 * Start the native window event loop.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_run(kc_wvw_t *ctx) {
    MSG message;

    if (!ctx) {
        return KC_WVW_ERROR;
    }

    ctx->running = 1;
    if (kc_wvw_navigate(ctx, ctx->opts.url) != KC_WVW_OK) {
        return KC_WVW_ERROR;
    }

    while (ctx->running) {
        BOOL rc = GetMessageW(&message, NULL, 0, 0);
        if (rc <= 0) {
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return KC_WVW_OK;
}

/**
 * Navigate the current WebView to a new URL.
 * @param ctx Window context.
 * @param url Destination URL.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_navigate(kc_wvw_t *ctx, const char *url) {
    char *next_url;
    wchar_t *target;
    HRESULT hr;

    if (!ctx || !url) {
        return KC_WVW_ERROR;
    }
    if (ctx->bridge.callback && !kc_wvw_bridge_url_trusted(&ctx->bridge, url)) {
        return KC_WVW_ERROR;
    }

    next_url = kc_wvw_strdup(url);
    if (!next_url) {
        return KC_WVW_ERROR;
    }
    free(ctx->pending_url);
    ctx->pending_url = next_url;

    if (!ctx->webview) {
        return KC_WVW_OK;
    }

    target = kc_wvw_utf16_from_utf8(ctx->pending_url);
    if (!target) {
        return KC_WVW_ERROR;
    }

    hr = ICoreWebView2_Navigate(ctx->webview, target);
    free(target);
    return FAILED(hr) ? KC_WVW_ERROR : KC_WVW_OK;
}

/**
 * Enable one native bridge with a fixed method whitelist.
 * @param ctx Window context.
 * @param opts Bridge configuration options.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_enable_bridge(kc_wvw_t *ctx, const kc_wvw_bridge_options_t *opts) {
    kc_wvw_bridge_state_t bridge;

    if (!ctx || !opts) {
        return KC_WVW_ERROR;
    }

    memset(&bridge, 0, sizeof(bridge));
    if (kc_wvw_bridge_state_copy(&bridge, opts) != KC_WVW_OK) {
        return KC_WVW_ERROR;
    }
    if (!kc_wvw_bridge_url_trusted(&bridge, ctx->pending_url ? ctx->pending_url : ctx->opts.url)) {
        kc_wvw_bridge_state_free(&bridge);
        return KC_WVW_ERROR;
    }

    kc_wvw_bridge_state_free(&ctx->bridge);
    ctx->bridge = bridge;
    if (ctx->webview) {
        if (kc_wvw_windows_install_bridge(ctx) != KC_WVW_OK) {
            kc_wvw_bridge_state_free(&ctx->bridge);
            return KC_WVW_ERROR;
        }
    }
    return KC_WVW_OK;
}

/**
 * Deliver one native bridge event into the current WebView.
 * @param ctx Window context.
 * @param json JSON payload to dispatch as the event detail.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_post_bridge_event(kc_wvw_t *ctx, const char *json) {
    return kc_wvw_bridge_post_json(ctx, json);
}

#else

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "wvw.h"

#include <gtk/gtk.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webkit2/webkit2.h>

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
    int sig;
    kc_wvw_signal_callback_t cb;
} kc_wvw_signal_entry_t;

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
    kc_wvw_signal_entry_t *signal_handlers;
    int n_signal_handlers;
    int signal_handlers_capacity;
    int running;
    GtkWidget *window;
    WebKitWebView *web_view;
    kc_wvw_bridge_state_t bridge;
};

static int kc_wvw_bridge_url_trusted(kc_wvw_bridge_state_t *bridge, const char *url);
static void kc_wvw_bridge_state_free(kc_wvw_bridge_state_t *bridge);
static int kc_wvw_bridge_state_copy(kc_wvw_bridge_state_t *dst, const kc_wvw_bridge_options_t *src);
static char *kc_wvw_bridge_bootstrap_script(kc_wvw_bridge_state_t *bridge);
static int kc_wvw_bridge_post_json(kc_wvw_t *ctx, const char *json);
static char *kc_wvw_bridge_dispatch_request(kc_wvw_t *ctx, const char *json);

static const kc_env_map_t env_config_table[] = {
    { "KC_WVW_URL", offsetof(kc_wvw_options_t, url), KC_ENV_TYPE_STR },
    { "KC_WVW_TITLE", offsetof(kc_wvw_options_t, title), KC_ENV_TYPE_STR },
    { "KC_WVW_BACKGROUND", offsetof(kc_wvw_options_t, background), KC_ENV_TYPE_STR },
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

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} kc_wvw_text_buf_t;

/**
 * Initialize one text buffer.
 * @param buf Buffer state.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
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

/**
 * Append one byte range into the text buffer.
 * @param buf Buffer state.
 * @param text Source byte range.
 * @param len Source length.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
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

/**
 * Append one C string into the text buffer.
 * @param buf Buffer state.
 * @param text Source text.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_text_buf_append(kc_wvw_text_buf_t *buf, const char *text) {
    if (!text) {
        return KC_WVW_ERROR;
    }

    return kc_wvw_text_buf_append_n(buf, text, strlen(text));
}

/**
 * Duplicate one byte range into a C string.
 * @param start Start of the range.
 * @param len Range length.
 * @return Newly allocated string or NULL.
 */
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

/**
 * Return whether one bridge method name is safe for JS property injection.
 * @param method Candidate method name.
 * @return Non-zero when the identifier is accepted, otherwise zero.
 */
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

/**
 * Return whether one URL is trusted for the current bridge policy.
 * @param bridge Bridge state.
 * @param url Candidate URL.
 * @return Non-zero when the URL is accepted, otherwise zero.
 */
static int kc_wvw_bridge_url_trusted(kc_wvw_bridge_state_t *bridge, const char *url) {
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

    return 0;
}

/**
 * Release one copied bridge configuration.
 * @param bridge Bridge state.
 * @return None.
 */
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

/**
 * Copy one bridge configuration into the context state.
 * @param dst Destination bridge state.
 * @param src Source bridge options.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
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

/**
 * Return whether one method belongs to the current whitelist.
 * @param bridge Bridge state.
 * @param method Candidate method name.
 * @return Non-zero when the method is accepted, otherwise zero.
 */
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

/**
 * Extract one string field from a bridge request.
 * @param json Source JSON payload.
 * @param key Field name.
 * @return Newly allocated string value or NULL.
 */
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

/**
 * Extract the params payload from one bridge request.
 * @param json Source JSON payload.
 * @return Newly allocated JSON fragment or NULL.
 */
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

/**
 * Build one bridge response payload.
 * @param id Request identifier.
 * @param ok Success flag.
 * @param body Serialized result or error object.
 * @return Newly allocated response payload or NULL.
 */
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

/**
 * Build one JSON error object.
 * @param code Error code string.
 * @param message Error message string.
 * @return Newly allocated JSON error object or NULL.
 */
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

/**
 * Build the injected NativeBridge bootstrap script.
 * @param bridge Bridge state.
 * @return Newly allocated script text or NULL.
 */
static char *kc_wvw_bridge_bootstrap_script(kc_wvw_bridge_state_t *bridge) {
    kc_wvw_text_buf_t buf;
    int i;

    if (!bridge || kc_wvw_text_buf_init(&buf) != KC_WVW_OK) {
        return NULL;
    }

    if (kc_wvw_text_buf_append(&buf, "(function(){if(window.NativeBridge){return;}var __kcWvwPending={};var __kcWvwSeq=0;window.__kcWvwReceive=function(msg){if(msg&&typeof msg.id==='string'){var cb=__kcWvwPending[msg.id];if(cb){delete __kcWvwPending[msg.id];cb(msg.ok?null:(msg.error||{code:'ERROR',message:'Bridge error'}),msg.ok?msg.result:null);}return;}window.dispatchEvent(new CustomEvent('") != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, KC_WVW_BRIDGE_EVENT_NAME) != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, "',{detail:msg}));};window.NativeBridge={};function __kcWvwSend(method,params,callback){var id=String(++__kcWvwSeq);if(typeof callback==='function'){__kcWvwPending[id]=callback;}window.webkit.messageHandlers.kc_wvw_native.postMessage(JSON.stringify({id:id,method:method,params:params===undefined?null:params}));}") != KC_WVW_OK) {
        free(buf.data);
        return NULL;
    }

    for (i = 0; i < bridge->method_count; i++) {
        if (kc_wvw_text_buf_append(&buf, "window.NativeBridge.") != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, bridge->methods[i]) != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, "=function(params,callback){return __kcWvwSend('") != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, bridge->methods[i]) != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, "',params,callback);};") != KC_WVW_OK) {
            free(buf.data);
            return NULL;
        }
    }

    if (kc_wvw_text_buf_append(&buf, "}());") != KC_WVW_OK) {
        free(buf.data);
        return NULL;
    }

    return buf.data;
}

/**
 * Escape one JSON payload for direct JavaScript evaluation.
 * @param json JSON payload.
 * @return Newly allocated escaped string or NULL.
 */
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

/**
 * Deliver one JSON payload into the WebView bridge runtime.
 * @param ctx Window context.
 * @param json JSON payload.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_bridge_post_json(kc_wvw_t *ctx, const char *json) {
    char *escaped;
    char *script;
    size_t cap;

    if (!ctx || !ctx->web_view || !json || strlen(json) > KC_WVW_BRIDGE_MAX_MESSAGE) {
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
    webkit_web_view_evaluate_javascript(ctx->web_view, script, -1, NULL, NULL, NULL, NULL, NULL);
    free(escaped);
    free(script);
    return KC_WVW_OK;
}

/**
 * Dispatch one bridge request into the application callback.
 * @param ctx Window context.
 * @param json Request payload.
 * @return Newly allocated response payload or NULL.
 */
static char *kc_wvw_bridge_dispatch_request(kc_wvw_t *ctx, const char *json) {
    char *id;
    char *method;
    char *params;
    char *result;
    char *error;
    char *response;
    int rc;

    if (!ctx || !json || strlen(json) > KC_WVW_BRIDGE_MAX_MESSAGE) {
        error = kc_wvw_bridge_error_object("BAD_REQUEST", "Bridge request is invalid.");
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
        error = kc_wvw_bridge_error_object("BAD_REQUEST", "Bridge request is malformed.");
        response = error ? kc_wvw_bridge_wrap_response("0", 0, error) : NULL;
        free(error);
        return response;
    }

    if (!kc_wvw_bridge_method_allowed(&ctx->bridge, method)) {
        error = kc_wvw_bridge_error_object("METHOD_NOT_ALLOWED", "Bridge method is not allowed.");
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
            result = kc_wvw_bridge_error_object("CALL_FAILED", "Bridge callback failed.");
        }
        response = result ? kc_wvw_bridge_wrap_response(id, 0, result) : NULL;
    }

    free(id);
    free(method);
    free(params);
    free(result);
    return response;
}

/**
 * Parses one hexadecimal WebView background color.
 * @param text Color text in RRGGBB or AARRGGBB format.
 * @param out_rgba Destination GTK RGBA value.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_parse_background_color(const char *text, GdkRGBA *out_rgba) {
    size_t length;
    unsigned int parts[4];

    if (!text || !out_rgba) {
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

    out_rgba->red = (gdouble)parts[1] / 255.0;
    out_rgba->green = (gdouble)parts[2] / 255.0;
    out_rgba->blue = (gdouble)parts[3] / 255.0;
    out_rgba->alpha = (gdouble)parts[0] / 255.0;
    return KC_WVW_OK;
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
 * Handle one message received from the injected NativeBridge runtime.
 * @param manager User content manager.
 * @param js_result JavaScript result.
 * @param user_data Window context.
 * @return None.
 */
static void kc_wvw_linux_bridge_message(WebKitUserContentManager *manager, WebKitJavascriptResult *js_result, gpointer user_data) {
    JSCValue *value;
    char *request;
    char *response;
    kc_wvw_t *ctx;

    (void)manager;
    ctx = (kc_wvw_t *)user_data;
    if (!ctx) {
        return;
    }

    value = webkit_javascript_result_get_js_value(js_result);
    request = value ? jsc_value_to_string(value) : NULL;
    if (!request) {
        return;
    }

    response = kc_wvw_bridge_dispatch_request(ctx, request);
    if (response) {
        kc_wvw_bridge_post_json(ctx, response);
        free(response);
    }
    g_free(request);
}

/**
 * Decide whether one navigation request stays inside the trusted WebView.
 * @param web_view Web view.
 * @param decision Policy decision.
 * @param type Policy decision type.
 * @param user_data Window context.
 * @return TRUE when the navigation is handled, otherwise FALSE.
 */
static gboolean kc_wvw_linux_bridge_policy(WebKitWebView *web_view, WebKitPolicyDecision *decision, WebKitPolicyDecisionType type, gpointer user_data) {
    WebKitNavigationPolicyDecision *nav;
    WebKitNavigationAction *action;
    WebKitURIRequest *request;
    const gchar *uri;
    kc_wvw_t *ctx;

    (void)web_view;
    ctx = (kc_wvw_t *)user_data;
    if (!ctx || !ctx->bridge.callback || type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
        return FALSE;
    }

    nav = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    action = webkit_navigation_policy_decision_get_navigation_action(nav);
    request = webkit_navigation_action_get_request(action);
    uri = request ? webkit_uri_request_get_uri(request) : NULL;
    if (uri && !kc_wvw_bridge_url_trusted(&ctx->bridge, uri)) {
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }

    return FALSE;
}

/**
 * Install the configured bridge into the current WebView.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_linux_install_bridge(kc_wvw_t *ctx) {
    WebKitUserContentManager *manager;
    char *script;

    if (!ctx || !ctx->web_view || !ctx->bridge.callback) {
        return KC_WVW_OK;
    }

    manager = webkit_web_view_get_user_content_manager(ctx->web_view);
    if (!manager) {
        return KC_WVW_ERROR;
    }

    script = kc_wvw_bridge_bootstrap_script(&ctx->bridge);
    if (!script) {
        return KC_WVW_ERROR;
    }

    webkit_user_content_manager_unregister_script_message_handler(manager, "kc_wvw_native");
    webkit_user_content_manager_register_script_message_handler(manager, "kc_wvw_native");
    g_signal_connect(manager, "script-message-received::kc_wvw_native", G_CALLBACK(kc_wvw_linux_bridge_message), ctx);
    webkit_user_content_manager_add_script(manager, webkit_user_script_new(script, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL));
    g_signal_connect(ctx->web_view, "decide-policy", G_CALLBACK(kc_wvw_linux_bridge_policy), ctx);
    free(script);
    return KC_WVW_OK;
}

/**
 * Create the native GTK window and WebView.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_linux_create_window(kc_wvw_t *ctx) {
    GdkRGBA background;

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
    if (ctx->opts.background) {
        if (kc_wvw_parse_background_color(ctx->opts.background, &background) != KC_WVW_OK) {
            fprintf(stderr, "wvw: invalid background color '%s'\n", ctx->opts.background);
            return KC_WVW_ERROR;
        }
        webkit_web_view_set_background_color(ctx->web_view, &background);
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
    free(opts->background);
    opts->url = NULL;
    opts->title = NULL;
    opts->background = NULL;
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
    ctx->opts.background = opts->background ? kc_wvw_strdup(opts->background) : NULL;
    ctx->opts.width = opts->width;
    ctx->opts.height = opts->height;
    ctx->opts.fullscreen = opts->fullscreen;
    ctx->opts.borderless = opts->borderless;

    if (!ctx->opts.url || !ctx->opts.title || (opts->background && !ctx->opts.background)) {
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
    kc_wvw_bridge_state_free(&ctx->bridge);
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
    if (ctx->bridge.callback && !kc_wvw_bridge_url_trusted(&ctx->bridge, url)) {
        return KC_WVW_ERROR;
    }

    webkit_web_view_load_uri(ctx->web_view, url);
    return KC_WVW_OK;
}

/**
 * Enable one native bridge with a fixed method whitelist.
 * @param ctx Window context.
 * @param opts Bridge configuration options.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_enable_bridge(kc_wvw_t *ctx, const kc_wvw_bridge_options_t *opts) {
    kc_wvw_bridge_state_t bridge;

    if (!ctx || !opts) {
        return KC_WVW_ERROR;
    }

    memset(&bridge, 0, sizeof(bridge));
    if (kc_wvw_bridge_state_copy(&bridge, opts) != KC_WVW_OK) {
        return KC_WVW_ERROR;
    }
    if (!kc_wvw_bridge_url_trusted(&bridge, ctx->opts.url)) {
        kc_wvw_bridge_state_free(&bridge);
        return KC_WVW_ERROR;
    }

    kc_wvw_bridge_state_free(&ctx->bridge);
    ctx->bridge = bridge;
    return kc_wvw_linux_install_bridge(ctx);
}

/**
 * Deliver one native bridge event into the current WebView.
 * @param ctx Window context.
 * @param json JSON payload to dispatch as the event detail.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_post_bridge_event(kc_wvw_t *ctx, const char *json) {
    return kc_wvw_bridge_post_json(ctx, json);
}

#endif
