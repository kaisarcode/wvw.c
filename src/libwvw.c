/**
 * libwvw.c - Core implementation for the wvw library.
 * Summary: Implements native WebView hosting.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "wvw.h"

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <ocidl.h>
#include <exdisp.h>
#include <olectl.h>
#else
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#endif

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

#ifdef _WIN32
typedef struct {
    IOleClientSite iface;
    struct kc_wvw *ctx;
} kc_wvw_client_site_t;

typedef struct {
    IOleInPlaceSite iface;
    struct kc_wvw *ctx;
} kc_wvw_inplace_site_t;

typedef struct {
    IOleInPlaceFrame iface;
    struct kc_wvw *ctx;
} kc_wvw_inplace_frame_t;

typedef struct {
    IDispatch iface;
    struct kc_wvw *ctx;
} kc_wvw_event_sink_t;
#endif

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

struct kc_wvw {
    kc_wvw_options_t opts;
    kc_wvw_signal_entry_t *signal_handlers;
    int n_signal_handlers;
    int signal_handlers_capacity;
    int running;
#ifdef _WIN32
    HWND hwnd;
    HINSTANCE hinstance;
    IOleObject *ole_object;
    IWebBrowser2 *browser;
    IConnectionPoint *connection_point;
    DWORD connection_cookie;
    IStorage *storage;
    ILockBytes *lock_bytes;
    kc_wvw_client_site_t client_site;
    kc_wvw_inplace_site_t inplace_site;
    kc_wvw_inplace_frame_t inplace_frame;
    kc_wvw_event_sink_t event_sink;
#else
    GtkWidget *window;
    WebKitWebView *web_view;
#endif
};

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

#ifdef _WIN32
/**
 * Convert one UTF-8 string to a Windows BSTR.
 * @param text UTF-8 input string.
 * @return Newly allocated BSTR or NULL.
 */
static BSTR kc_wvw_bstr_from_utf8(const char *text) {
    int wlen;
    BSTR out;
    wchar_t *wide;

    if (!text) {
        return NULL;
    }

    wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wlen <= 0) {
        return NULL;
    }

    wide = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wide) {
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, wlen) <= 0) {
        free(wide);
        return NULL;
    }

    out = SysAllocString(wide);
    free(wide);
    return out;
}

/**
 * Return the containing window for one COM site object.
 * @param ctx Window context.
 * @return Native window handle.
 */
static HWND kc_wvw_window_handle(kc_wvw_t *ctx) {
    return ctx ? ctx->hwnd : NULL;
}

/**
 * Resize the embedded browser object to the client area.
 * @param ctx Window context.
 * @return None.
 */
static void kc_wvw_windows_resize_browser(kc_wvw_t *ctx) {
    IOleInPlaceObject *inplace_object;
    RECT rect;

    if (!ctx || !ctx->ole_object || !ctx->hwnd) {
        return;
    }

    if (FAILED(IOleObject_QueryInterface(ctx->ole_object, &IID_IOleInPlaceObject, (void **)&inplace_object))) {
        return;
    }

    GetClientRect(ctx->hwnd, &rect);
    IOleInPlaceObject_SetObjectRects(inplace_object, &rect, &rect);
    IOleInPlaceObject_Release(inplace_object);
}

/**
 * Process one browser event callback.
 * @param ctx Window context.
 * @param dispid Event identifier.
 * @param params Event argument array.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_windows_event(kc_wvw_t *ctx, DISPID dispid, DISPPARAMS *params) {
    (void)params;

    if (!ctx) {
        return KC_WVW_ERROR;
    }

    (void)dispid;
    return KC_WVW_OK;
}

/**
 * Return one OLE interface pointer from the window context.
 * @param ctx Window context.
 * @param riid Requested interface identifier.
 * @param out_value Destination interface pointer.
 * @return HRESULT success code.
 */
static HRESULT kc_wvw_windows_query_site_interface(kc_wvw_t *ctx, REFIID riid, void **out_value) {
    if (!ctx || !out_value) {
        return E_POINTER;
    }

    *out_value = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IOleClientSite)) {
        *out_value = &ctx->client_site.iface;
    } else if (IsEqualIID(riid, &IID_IOleInPlaceSite)) {
        *out_value = &ctx->inplace_site.iface;
    } else if (IsEqualIID(riid, &IID_IOleWindow)) {
        *out_value = &ctx->inplace_site.iface;
    } else if (IsEqualIID(riid, &IID_IOleInPlaceFrame)) {
        *out_value = &ctx->inplace_frame.iface;
    }

    if (*out_value) {
        IUnknown_AddRef((IUnknown *)*out_value);
        return S_OK;
    }

    return E_NOINTERFACE;
}

/**
 * Query one interface from the client site object.
 * @param iface COM interface pointer.
 * @param riid Requested interface identifier.
 * @param out_value Destination interface pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_client_site_query_interface(IOleClientSite *iface, REFIID riid, void **out_value) {
    return kc_wvw_windows_query_site_interface(((kc_wvw_client_site_t *)iface)->ctx, riid, out_value);
}

/**
 * Increment the client site reference count.
 * @param iface COM interface pointer.
 * @return Constant reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_client_site_add_ref(IOleClientSite *iface) {
    (void)iface;
    return 1;
}

/**
 * Decrement the client site reference count.
 * @param iface COM interface pointer.
 * @return Constant reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_client_site_release(IOleClientSite *iface) {
    (void)iface;
    return 1;
}

/**
 * Persist one embedded object request.
 * @param iface COM interface pointer.
 * @return E_NOTIMPL.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_client_site_save_object(IOleClientSite *iface) {
    (void)iface;
    return E_NOTIMPL;
}

/**
 * Resolve one moniker from the client site.
 * @param iface COM interface pointer.
 * @return E_NOTIMPL.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_client_site_get_moniker(IOleClientSite *iface, DWORD assign, DWORD which, IMoniker **out_moniker) {
    (void)iface;
    (void)assign;
    (void)which;
    (void)out_moniker;
    return E_NOTIMPL;
}

/**
 * Return the OLE container for the client site.
 * @param iface COM interface pointer.
 * @return E_NOINTERFACE.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_client_site_get_container(IOleClientSite *iface, IOleContainer **out_container) {
    (void)iface;
    if (out_container) {
        *out_container = NULL;
    }
    return E_NOINTERFACE;
}

/**
 * Notify that the embedded object became visible.
 * @param iface COM interface pointer.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_client_site_show_object(IOleClientSite *iface) {
    (void)iface;
    return S_OK;
}

/**
 * Notify one visibility change request.
 * @param iface COM interface pointer.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_client_site_on_show_window(IOleClientSite *iface, BOOL show) {
    (void)iface;
    (void)show;
    return S_OK;
}

/**
 * Request a new object layout from the client site.
 * @param iface COM interface pointer.
 * @return E_NOTIMPL.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_client_site_request_new_object_layout(IOleClientSite *iface) {
    (void)iface;
    return E_NOTIMPL;
}

/**
 * Query one interface from the in-place site object.
 * @param iface COM interface pointer.
 * @param riid Requested interface identifier.
 * @param out_value Destination interface pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_site_query_interface(IOleInPlaceSite *iface, REFIID riid, void **out_value) {
    return kc_wvw_windows_query_site_interface(((kc_wvw_inplace_site_t *)iface)->ctx, riid, out_value);
}

/**
 * Increment the in-place site reference count.
 * @param iface COM interface pointer.
 * @return Constant reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_inplace_site_add_ref(IOleInPlaceSite *iface) {
    (void)iface;
    return 1;
}

/**
 * Decrement the in-place site reference count.
 * @param iface COM interface pointer.
 * @return Constant reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_inplace_site_release(IOleInPlaceSite *iface) {
    (void)iface;
    return 1;
}

/**
 * Return the native host window handle.
 * @param iface COM interface pointer.
 * @param out_window Destination window handle.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_site_get_window(IOleInPlaceSite *iface, HWND *out_window) {
    kc_wvw_t *ctx = ((kc_wvw_inplace_site_t *)iface)->ctx;

    if (!out_window) {
        return E_POINTER;
    }

    *out_window = kc_wvw_window_handle(ctx);
    return *out_window ? S_OK : E_FAIL;
}

/**
 * Return the current interaction state.
 * @param iface COM interface pointer.
 * @return S_FALSE.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_site_context_sensitive_help(IOleInPlaceSite *iface, BOOL enter_mode) {
    (void)iface;
    (void)enter_mode;
    return S_FALSE;
}

/**
 * Confirm in-place activation support.
 * @param iface COM interface pointer.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_site_can_in_place_activate(IOleInPlaceSite *iface) {
    (void)iface;
    return S_OK;
}

/**
 * Notify that in-place activation started.
 * @param iface COM interface pointer.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_site_on_in_place_activate(IOleInPlaceSite *iface) {
    (void)iface;
    return S_OK;
}

/**
 * Notify that the UI became active.
 * @param iface COM interface pointer.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_site_on_ui_activate(IOleInPlaceSite *iface) {
    (void)iface;
    return S_OK;
}

/**
 * Provide host frame details for the in-place object.
 * @param iface COM interface pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_site_get_window_context(IOleInPlaceSite *iface, IOleInPlaceFrame **out_frame, IOleInPlaceUIWindow **out_doc, LPRECT pos_rect, LPRECT clip_rect, OLEINPLACEFRAMEINFO *frame_info) {
    kc_wvw_t *ctx = ((kc_wvw_inplace_site_t *)iface)->ctx;

    if (out_frame) {
        *out_frame = &ctx->inplace_frame.iface;
        IOleInPlaceFrame_AddRef(*out_frame);
    }
    if (out_doc) {
        *out_doc = NULL;
    }
    if (pos_rect) {
        GetClientRect(ctx->hwnd, pos_rect);
    }
    if (clip_rect) {
        GetClientRect(ctx->hwnd, clip_rect);
    }
    if (frame_info) {
        memset(frame_info, 0, sizeof(*frame_info));
        frame_info->cb = sizeof(*frame_info);
        frame_info->hwndFrame = ctx->hwnd;
    }
    return S_OK;
}

/**
 * Scroll request for the in-place object.
 * @param iface COM interface pointer.
 * @return E_NOTIMPL.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_site_scroll(IOleInPlaceSite *iface, SIZE extent) {
    (void)iface;
    (void)extent;
    return E_NOTIMPL;
}

/**
 * Notify that UI deactivation occurred.
 * @param iface COM interface pointer.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_site_on_ui_deactivate(IOleInPlaceSite *iface, BOOL undoable) {
    (void)iface;
    (void)undoable;
    return S_OK;
}

/**
 * Notify that in-place deactivation occurred.
 * @param iface COM interface pointer.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_site_on_in_place_deactivate(IOleInPlaceSite *iface) {
    (void)iface;
    return S_OK;
}

/**
 * Discard the undo state for the in-place object.
 * @param iface COM interface pointer.
 * @return E_NOTIMPL.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_site_discard_undo_state(IOleInPlaceSite *iface) {
    (void)iface;
    return E_NOTIMPL;
}

/**
 * Deactivate and undo the current in-place interaction.
 * @param iface COM interface pointer.
 * @return E_NOTIMPL.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_site_deactivate_and_undo(IOleInPlaceSite *iface) {
    (void)iface;
    return E_NOTIMPL;
}

/**
 * Apply a new object rectangle to the in-place object.
 * @param iface COM interface pointer.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_site_on_pos_rect_change(IOleInPlaceSite *iface, LPCRECT pos_rect) {
    kc_wvw_t *ctx = ((kc_wvw_inplace_site_t *)iface)->ctx;
    IOleInPlaceObject *inplace_object;

    if (!ctx || !ctx->ole_object || !pos_rect) {
        return E_FAIL;
    }

    if (FAILED(IOleObject_QueryInterface(ctx->ole_object, &IID_IOleInPlaceObject, (void **)&inplace_object))) {
        return E_FAIL;
    }

    IOleInPlaceObject_SetObjectRects(inplace_object, pos_rect, pos_rect);
    IOleInPlaceObject_Release(inplace_object);
    return S_OK;
}

/**
 * Query one interface from the in-place frame object.
 * @param iface COM interface pointer.
 * @param riid Requested interface identifier.
 * @param out_value Destination interface pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_frame_query_interface(IOleInPlaceFrame *iface, REFIID riid, void **out_value) {
    return kc_wvw_windows_query_site_interface(((kc_wvw_inplace_frame_t *)iface)->ctx, riid, out_value);
}

/**
 * Increment the in-place frame reference count.
 * @param iface COM interface pointer.
 * @return Constant reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_inplace_frame_add_ref(IOleInPlaceFrame *iface) {
    (void)iface;
    return 1;
}

/**
 * Decrement the in-place frame reference count.
 * @param iface COM interface pointer.
 * @return Constant reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_inplace_frame_release(IOleInPlaceFrame *iface) {
    (void)iface;
    return 1;
}

/**
 * Return the native frame window handle.
 * @param iface COM interface pointer.
 * @param out_window Destination window handle.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_frame_get_window(IOleInPlaceFrame *iface, HWND *out_window) {
    return kc_wvw_inplace_site_get_window(&((kc_wvw_inplace_frame_t *)iface)->ctx->inplace_site.iface, out_window);
}

/**
 * Return the current frame help mode.
 * @param iface COM interface pointer.
 * @return S_FALSE.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_frame_context_sensitive_help(IOleInPlaceFrame *iface, BOOL enter_mode) {
    (void)iface;
    (void)enter_mode;
    return S_FALSE;
}

/**
 * Insert frame menus.
 * @param iface COM interface pointer.
 * @return E_NOTIMPL.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_frame_get_border(IOleInPlaceFrame *iface, LPRECT out_rect) {
    (void)iface;
    (void)out_rect;
    return E_NOTIMPL;
}

/**
 * Request border space from the frame.
 * @param iface COM interface pointer.
 * @return E_NOTIMPL.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_frame_request_border_space(IOleInPlaceFrame *iface, LPCBORDERWIDTHS widths) {
    (void)iface;
    (void)widths;
    return E_NOTIMPL;
}

/**
 * Apply border space to the frame.
 * @param iface COM interface pointer.
 * @return E_NOTIMPL.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_frame_set_border_space(IOleInPlaceFrame *iface, LPCBORDERWIDTHS widths) {
    (void)iface;
    (void)widths;
    return E_NOTIMPL;
}

/**
 * Register active object UI state.
 * @param iface COM interface pointer.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_frame_set_active_object(IOleInPlaceFrame *iface, IOleInPlaceActiveObject *active_object, LPCOLESTR object_name) {
    (void)iface;
    (void)active_object;
    (void)object_name;
    return S_OK;
}

/**
 * Insert frame menus.
 * @param iface COM interface pointer.
 * @return E_NOTIMPL.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_frame_insert_menus(IOleInPlaceFrame *iface, HMENU shared_menu, LPOLEMENUGROUPWIDTHS menu_widths) {
    (void)iface;
    (void)shared_menu;
    (void)menu_widths;
    return E_NOTIMPL;
}

/**
 * Apply the shared menu to the frame.
 * @param iface COM interface pointer.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_frame_set_menu(IOleInPlaceFrame *iface, HMENU shared_menu, HOLEMENU holemenu, HWND active_window) {
    (void)iface;
    (void)shared_menu;
    (void)holemenu;
    (void)active_window;
    return S_OK;
}

/**
 * Remove any shared menus from the frame.
 * @param iface COM interface pointer.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_frame_remove_menus(IOleInPlaceFrame *iface, HMENU shared_menu) {
    (void)iface;
    (void)shared_menu;
    return S_OK;
}

/**
 * Update status text in the frame.
 * @param iface COM interface pointer.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_frame_set_status_text(IOleInPlaceFrame *iface, LPCOLESTR status_text) {
    (void)iface;
    (void)status_text;
    return S_OK;
}

/**
 * Enable frame-level modeless interaction.
 * @param iface COM interface pointer.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_frame_enable_modeless(IOleInPlaceFrame *iface, BOOL enable) {
    (void)iface;
    (void)enable;
    return S_OK;
}

/**
 * Translate one frame accelerator.
 * @param iface COM interface pointer.
 * @return E_NOTIMPL.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_inplace_frame_translate_accelerator(IOleInPlaceFrame *iface, LPMSG msg, WORD short_id) {
    (void)iface;
    (void)msg;
    (void)short_id;
    return E_NOTIMPL;
}

/**
 * Query one interface from the browser event sink.
 * @param iface COM interface pointer.
 * @param riid Requested interface identifier.
 * @param out_value Destination interface pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_event_sink_query_interface(IDispatch *iface, REFIID riid, void **out_value) {
    kc_wvw_event_sink_t *sink = (kc_wvw_event_sink_t *)iface;

    if (!out_value) {
        return E_POINTER;
    }

    *out_value = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &DIID_DWebBrowserEvents2)) {
        *out_value = &sink->iface;
        IDispatch_AddRef(&sink->iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

/**
 * Increment the browser event sink reference count.
 * @param iface COM interface pointer.
 * @return Constant reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_event_sink_add_ref(IDispatch *iface) {
    (void)iface;
    return 1;
}

/**
 * Decrement the browser event sink reference count.
 * @param iface COM interface pointer.
 * @return Constant reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_event_sink_release(IDispatch *iface) {
    (void)iface;
    return 1;
}

/**
 * Return the event sink type-info count.
 * @param iface COM interface pointer.
 * @param out_count Destination type-info count.
 * @return S_OK.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_event_sink_get_type_info_count(IDispatch *iface, UINT *out_count) {
    (void)iface;
    if (out_count) {
        *out_count = 0;
    }
    return S_OK;
}

/**
 * Return type-info from the event sink.
 * @param iface COM interface pointer.
 * @return E_NOTIMPL.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_event_sink_get_type_info(IDispatch *iface, UINT info_index, LCID lcid, ITypeInfo **out_info) {
    (void)iface;
    (void)info_index;
    (void)lcid;
    (void)out_info;
    return E_NOTIMPL;
}

/**
 * Resolve dispatch names for the event sink.
 * @param iface COM interface pointer.
 * @return E_NOTIMPL.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_event_sink_get_ids_of_names(IDispatch *iface, REFIID riid, LPOLESTR *names, UINT name_count, LCID lcid, DISPID *out_dispids) {
    (void)iface;
    (void)riid;
    (void)names;
    (void)name_count;
    (void)lcid;
    (void)out_dispids;
    return E_NOTIMPL;
}

/**
 * Dispatch one browser event into the native window context.
 * @param iface COM interface pointer.
 * @param dispid Event identifier.
 * @param params Event argument array.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_event_sink_invoke(IDispatch *iface, DISPID dispid, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *params, VARIANT *result, EXCEPINFO *exception_info, UINT *arg_error) {
    (void)riid;
    (void)lcid;
    (void)flags;
    (void)result;
    (void)exception_info;
    (void)arg_error;

    kc_wvw_windows_event(((kc_wvw_event_sink_t *)iface)->ctx, dispid, params);
    return S_OK;
}

static IOleClientSiteVtbl kc_wvw_client_site_vtbl = {
    kc_wvw_client_site_query_interface,
    kc_wvw_client_site_add_ref,
    kc_wvw_client_site_release,
    kc_wvw_client_site_save_object,
    kc_wvw_client_site_get_moniker,
    kc_wvw_client_site_get_container,
    kc_wvw_client_site_show_object,
    kc_wvw_client_site_on_show_window,
    kc_wvw_client_site_request_new_object_layout,
};

static IOleInPlaceSiteVtbl kc_wvw_inplace_site_vtbl = {
    kc_wvw_inplace_site_query_interface,
    kc_wvw_inplace_site_add_ref,
    kc_wvw_inplace_site_release,
    kc_wvw_inplace_site_get_window,
    kc_wvw_inplace_site_context_sensitive_help,
    kc_wvw_inplace_site_can_in_place_activate,
    kc_wvw_inplace_site_on_in_place_activate,
    kc_wvw_inplace_site_on_ui_activate,
    kc_wvw_inplace_site_get_window_context,
    kc_wvw_inplace_site_scroll,
    kc_wvw_inplace_site_on_ui_deactivate,
    kc_wvw_inplace_site_on_in_place_deactivate,
    kc_wvw_inplace_site_discard_undo_state,
    kc_wvw_inplace_site_deactivate_and_undo,
    kc_wvw_inplace_site_on_pos_rect_change,
};

static IOleInPlaceFrameVtbl kc_wvw_inplace_frame_vtbl = {
    kc_wvw_inplace_frame_query_interface,
    kc_wvw_inplace_frame_add_ref,
    kc_wvw_inplace_frame_release,
    kc_wvw_inplace_frame_get_window,
    kc_wvw_inplace_frame_context_sensitive_help,
    kc_wvw_inplace_frame_get_border,
    kc_wvw_inplace_frame_request_border_space,
    kc_wvw_inplace_frame_set_border_space,
    kc_wvw_inplace_frame_set_active_object,
    kc_wvw_inplace_frame_insert_menus,
    kc_wvw_inplace_frame_set_menu,
    kc_wvw_inplace_frame_remove_menus,
    kc_wvw_inplace_frame_set_status_text,
    kc_wvw_inplace_frame_enable_modeless,
    kc_wvw_inplace_frame_translate_accelerator,
};

static IDispatchVtbl kc_wvw_event_sink_vtbl = {
    kc_wvw_event_sink_query_interface,
    kc_wvw_event_sink_add_ref,
    kc_wvw_event_sink_release,
    kc_wvw_event_sink_get_type_info_count,
    kc_wvw_event_sink_get_type_info,
    kc_wvw_event_sink_get_ids_of_names,
    kc_wvw_event_sink_invoke,
};

/**
 * Window procedure for the native host window.
 * @param hwnd Window handle.
 * @param msg Window message.
 * @param wparam Message parameter.
 * @param lparam Message parameter.
 * @return Window-procedure result.
 */
static LRESULT CALLBACK kc_wvw_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    kc_wvw_t *ctx = (kc_wvw_t *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCT *create = (CREATESTRUCT *)lparam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
    case WM_SIZE:
        kc_wvw_windows_resize_browser(ctx);
        return 0;
    case WM_DESTROY:
        if (ctx) {
            ctx->running = 0;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

/**
 * Create the native Windows host window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_windows_create_window(kc_wvw_t *ctx) {
    WNDCLASSW klass;
    DWORD style;
    DWORD ex_style;
    RECT rect;
    BSTR title;
    wchar_t class_name[] = L"kc_wvw_window";

    memset(&klass, 0, sizeof(klass));
    klass.lpfnWndProc = kc_wvw_window_proc;
    klass.hInstance = GetModuleHandleW(NULL);
    klass.lpszClassName = class_name;
    klass.hCursor = LoadCursor(NULL, IDC_ARROW);
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

    title = kc_wvw_bstr_from_utf8(ctx->opts.title ? ctx->opts.title : "wvw");
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
        klass.hInstance,
        ctx
    );
    SysFreeString(title);

    if (!ctx->hwnd) {
        return KC_WVW_ERROR;
    }

    ShowWindow(ctx->hwnd, SW_SHOWDEFAULT);
    UpdateWindow(ctx->hwnd);
    return KC_WVW_OK;
}

/**
 * Create an in-memory COM storage object for the browser control.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_windows_create_storage(kc_wvw_t *ctx) {
    HRESULT hr;

    hr = CreateILockBytesOnHGlobal(NULL, TRUE, &ctx->lock_bytes);
    if (FAILED(hr)) {
        return KC_WVW_ERROR;
    }

    hr = StgCreateDocfileOnILockBytes(ctx->lock_bytes, STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_READWRITE, 0, &ctx->storage);
    return FAILED(hr) ? KC_WVW_ERROR : KC_WVW_OK;
}

/**
 * Connect the browser event sink to DWebBrowserEvents2.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_windows_advise_events(kc_wvw_t *ctx) {
    IConnectionPointContainer *container;
    HRESULT hr;

    hr = IWebBrowser2_QueryInterface(ctx->browser, &IID_IConnectionPointContainer, (void **)&container);
    if (FAILED(hr) || !container) {
        return KC_WVW_ERROR;
    }

    hr = IConnectionPointContainer_FindConnectionPoint(container, &DIID_DWebBrowserEvents2, &ctx->connection_point);
    IConnectionPointContainer_Release(container);
    if (FAILED(hr) || !ctx->connection_point) {
        return KC_WVW_ERROR;
    }

    hr = IConnectionPoint_Advise(ctx->connection_point, (IUnknown *)&ctx->event_sink.iface, &ctx->connection_cookie);
    return FAILED(hr) ? KC_WVW_ERROR : KC_WVW_OK;
}

/**
 * Create and activate the embedded Internet Explorer browser control.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_windows_create_browser(kc_wvw_t *ctx) {
    RECT rect;
    HRESULT hr;

    hr = OleCreate(&CLSID_WebBrowser, &IID_IOleObject, OLERENDER_DRAW, NULL, &ctx->client_site.iface, ctx->storage, (void **)&ctx->ole_object);
    if (FAILED(hr) || !ctx->ole_object) {
        return KC_WVW_ERROR;
    }

    IOleObject_SetClientSite(ctx->ole_object, &ctx->client_site.iface);
    IOleObject_SetHostNames(ctx->ole_object, L"wvw", NULL);
    OleSetContainedObject((IUnknown *)ctx->ole_object, TRUE);

    GetClientRect(ctx->hwnd, &rect);
    hr = IOleObject_DoVerb(ctx->ole_object, OLEIVERB_SHOW, NULL, &ctx->client_site.iface, 0, ctx->hwnd, &rect);
    if (FAILED(hr)) {
        return KC_WVW_ERROR;
    }

    hr = IOleObject_QueryInterface(ctx->ole_object, &IID_IWebBrowser2, (void **)&ctx->browser);
    if (FAILED(hr) || !ctx->browser) {
        return KC_WVW_ERROR;
    }

    IWebBrowser2_put_AddressBar(ctx->browser, VARIANT_FALSE);
    IWebBrowser2_put_MenuBar(ctx->browser, VARIANT_FALSE);
    IWebBrowser2_put_StatusBar(ctx->browser, VARIANT_FALSE);
    IWebBrowser2_put_ToolBar(ctx->browser, 0);
    IWebBrowser2_put_Resizable(ctx->browser, ctx->opts.borderless ? VARIANT_FALSE : VARIANT_TRUE);
    IWebBrowser2_put_Silent(ctx->browser, VARIANT_TRUE);

    return kc_wvw_windows_advise_events(ctx);
}
#else

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
#endif

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
#ifndef _WIN32
    signal(sig_id, kc_wvw_signal_listener);
#else
    (void)sig_id;
#endif
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

#ifdef _WIN32
    ctx->client_site.iface.lpVtbl = &kc_wvw_client_site_vtbl;
    ctx->client_site.ctx = ctx;
    ctx->inplace_site.iface.lpVtbl = &kc_wvw_inplace_site_vtbl;
    ctx->inplace_site.ctx = ctx;
    ctx->inplace_frame.iface.lpVtbl = &kc_wvw_inplace_frame_vtbl;
    ctx->inplace_frame.ctx = ctx;
    ctx->event_sink.iface.lpVtbl = &kc_wvw_event_sink_vtbl;
    ctx->event_sink.ctx = ctx;

    if (FAILED(CoInitialize(NULL))) {
        kc_wvw_close(ctx);
        return KC_WVW_ERROR;
    }
    ctx->hinstance = GetModuleHandleW(NULL);
    if (kc_wvw_windows_create_window(ctx) != KC_WVW_OK ||
        kc_wvw_windows_create_storage(ctx) != KC_WVW_OK ||
        kc_wvw_windows_create_browser(ctx) != KC_WVW_OK) {
        kc_wvw_close(ctx);
        return KC_WVW_ERROR;
    }
#else
    if (!gtk_init_check(NULL, NULL)) {
        kc_wvw_close(ctx);
        return KC_WVW_ERROR;
    }
    if (kc_wvw_linux_create_window(ctx) != KC_WVW_OK) {
        kc_wvw_close(ctx);
        return KC_WVW_ERROR;
    }
#endif

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

#ifdef _WIN32
    if (ctx->connection_point && ctx->connection_cookie) {
        IConnectionPoint_Unadvise(ctx->connection_point, ctx->connection_cookie);
    }
    if (ctx->browser) {
        IWebBrowser2_Stop(ctx->browser);
        IWebBrowser2_Release(ctx->browser);
    }
    if (ctx->ole_object) {
        IOleObject_Close(ctx->ole_object, OLECLOSE_NOSAVE);
        IOleObject_Release(ctx->ole_object);
    }
    if (ctx->connection_point) {
        IConnectionPoint_Release(ctx->connection_point);
    }
    if (ctx->storage) {
        IStorage_Release(ctx->storage);
    }
    if (ctx->lock_bytes) {
        ILockBytes_Release(ctx->lock_bytes);
    }
    if (ctx->hwnd) {
        DestroyWindow(ctx->hwnd);
    }
    CoUninitialize();
#else
    if (ctx->window) {
        gtk_widget_destroy(ctx->window);
    }
#endif

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

#ifdef _WIN32
    while (ctx->running) {
        MSG message;
        int rc = GetMessage(&message, NULL, 0, 0);
        if (rc <= 0) {
            break;
        }
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
#else
    gtk_main();
#endif

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

#ifdef _WIN32
    {
        BSTR target = kc_wvw_bstr_from_utf8(url);
        VARIANT empty;
        HRESULT hr;

        if (!target) {
            return KC_WVW_ERROR;
        }

        VariantInit(&empty);
        empty.vt = VT_EMPTY;
        hr = IWebBrowser2_Navigate(ctx->browser, target, &empty, &empty, &empty, &empty);
        SysFreeString(target);
        return FAILED(hr) ? KC_WVW_ERROR : KC_WVW_OK;
    }
#else
    webkit_web_view_load_uri(ctx->web_view, url);
    return KC_WVW_OK;
#endif
}
