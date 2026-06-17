/**
 * libwvw_win.cpp - Windows implementation for the wvw library.
 * Summary: Hosts Microsoft Edge WebView2 in a native Win32 window.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "wvw.h"

#include <WebView2.h>
#include <shlobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define KC_WVW_CLOSE_MESSAGE (WM_APP + 1)

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

struct kc_wvw {
    kc_wvw_options_t opts;
    kc_wvw_signal_entry_t *signal_handlers;
    int n_signal_handlers;
    int signal_handlers_capacity;
    int running;
    int closing;
    int com_initialized;
    HWND hwnd;
    HINSTANCE hinstance;
    HMODULE loader;
    ICoreWebView2Environment *environment;
    ICoreWebView2Controller *controller;
    ICoreWebView2 *webview;
    kc_wvw_init_state_t init_state;
    char *pending_url;
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
 * Handles WebView2 environment creation completion.
 * @return COM handler object.
 */
class kc_wvw_environment_handler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
public:

    /**
     * Initializes the environment completion handler.
     * @param next_ctx Window context.
     * @return None.
     */
    explicit kc_wvw_environment_handler(kc_wvw_t *next_ctx) : ref_count(1), ctx(next_ctx) {
    }

    /**
     * Releases handler storage.
     * @return None.
     */
    virtual ~kc_wvw_environment_handler() {
    }

    /**
     * Returns one requested COM interface from this object.
     * @param riid Requested interface identifier.
     * @param object Destination pointer.
     * @return HRESULT success code.
     */
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
        if (!object) {
            return E_POINTER;
        }
        *object = NULL;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
            *object = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    /**
     * Increments the COM reference count.
     * @return New reference count.
     */
    ULONG STDMETHODCALLTYPE AddRef(void) override {
        return InterlockedIncrement(&ref_count);
    }

    /**
     * Decrements the COM reference count.
     * @return New reference count.
     */
    ULONG STDMETHODCALLTYPE Release(void) override {
        ULONG next = InterlockedDecrement(&ref_count);
        if (next == 0) {
            delete this;
        }
        return next;
    }

    /**
     * Receives the created WebView2 environment.
     * @param error_code Completion status.
     * @param result Created environment.
     * @return HRESULT success code.
     */
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT error_code, ICoreWebView2Environment *result) override;

private:

    volatile LONG ref_count;
    kc_wvw_t *ctx;
};

/**
 * Handles WebView2 controller creation completion.
 * @return COM handler object.
 */
class kc_wvw_controller_handler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
public:

    /**
     * Initializes the controller completion handler.
     * @param next_ctx Window context.
     * @return None.
     */
    explicit kc_wvw_controller_handler(kc_wvw_t *next_ctx) : ref_count(1), ctx(next_ctx) {
    }

    /**
     * Releases handler storage.
     * @return None.
     */
    virtual ~kc_wvw_controller_handler() {
    }

    /**
     * Returns one requested COM interface from this object.
     * @param riid Requested interface identifier.
     * @param object Destination pointer.
     * @return HRESULT success code.
     */
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
        if (!object) {
            return E_POINTER;
        }
        *object = NULL;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
            *object = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    /**
     * Increments the COM reference count.
     * @return New reference count.
     */
    ULONG STDMETHODCALLTYPE AddRef(void) override {
        return InterlockedIncrement(&ref_count);
    }

    /**
     * Decrements the COM reference count.
     * @return New reference count.
     */
    ULONG STDMETHODCALLTYPE Release(void) override {
        ULONG next = InterlockedDecrement(&ref_count);
        if (next == 0) {
            delete this;
        }
        return next;
    }

    /**
     * Receives the created WebView2 controller.
     * @param error_code Completion status.
     * @param result Created controller.
     * @return HRESULT success code.
     */
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT error_code, ICoreWebView2Controller *result) override;

private:

    volatile LONG ref_count;
    kc_wvw_t *ctx;
};

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
extern "C" uint64_t kc_wvw_version(void) {
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

    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &local_app_data))) {
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
    ctx->controller->put_Bounds(bounds);
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

    if (FAILED(ctx->webview->get_Settings(&settings)) || !settings) {
        return;
    }

    settings->put_IsStatusBarEnabled(FALSE);
    settings->put_IsZoomControlEnabled(FALSE);
#ifdef NDEBUG
    settings->put_AreDevToolsEnabled(FALSE);
    settings->put_AreDefaultContextMenusEnabled(FALSE);
#endif
    if (SUCCEEDED(settings->QueryInterface(IID_ICoreWebView2Settings3, (void **)&settings3)) && settings3) {
        settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
        settings3->Release();
    }
    settings->Release();
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

    hr = ctx->webview->Navigate(target);
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
    case WM_SETFOCUS:
        if (ctx && ctx->controller) {
            ctx->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
        return 0;
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
    kc_wvw_environment_handler *handler;
    wchar_t *user_data_dir;
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

    handler = new kc_wvw_environment_handler(ctx);
    hr = create_environment(NULL, user_data_dir, NULL, handler);
    handler->Release();
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
HRESULT STDMETHODCALLTYPE kc_wvw_environment_handler::Invoke(HRESULT error_code, ICoreWebView2Environment *result) {
    kc_wvw_controller_handler *handler;
    HRESULT hr;

    if (!ctx || ctx->closing || FAILED(error_code) || !result) {
        if (ctx) {
            ctx->init_state = KC_WVW_INIT_FAILED;
        }
        return S_OK;
    }

    ctx->environment = result;
    ctx->environment->AddRef();
    handler = new kc_wvw_controller_handler(ctx);
    hr = ctx->environment->CreateCoreWebView2Controller(ctx->hwnd, handler);
    handler->Release();
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
HRESULT STDMETHODCALLTYPE kc_wvw_controller_handler::Invoke(HRESULT error_code, ICoreWebView2Controller *result) {
    if (!ctx || ctx->closing || FAILED(error_code) || !result) {
        if (ctx) {
            ctx->init_state = KC_WVW_INIT_FAILED;
        }
        return S_OK;
    }

    ctx->controller = result;
    ctx->controller->AddRef();
    if (FAILED(ctx->controller->get_CoreWebView2(&ctx->webview)) || !ctx->webview) {
        ctx->init_state = KC_WVW_INIT_FAILED;
        return S_OK;
    }

    kc_wvw_update_bounds(ctx);
    kc_wvw_apply_settings(ctx);
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
extern "C" kc_wvw_options_t kc_wvw_options_default(void) {
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
extern "C" void kc_wvw_options_load_env(kc_wvw_options_t *opts) {
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
extern "C" void kc_wvw_options_free(kc_wvw_options_t *opts) {
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
extern "C" int kc_wvw_on_signal(kc_wvw_t *ctx, int sig, kc_wvw_signal_callback_t cb) {
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
extern "C" int kc_wvw_raise_signal(kc_wvw_t *ctx, int sig) {
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
extern "C" int kc_wvw_listen_signals(kc_wvw_t *ctx) {
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
extern "C" int kc_wvw_listen_signal(kc_wvw_t *ctx, int sig_id) {
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
extern "C" void kc_wvw_signal_listener(int sig) {
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
extern "C" int kc_wvw_open(kc_wvw_t **ctx_out, kc_wvw_options_t *opts) {
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
    ctx->opts.width = opts->width;
    ctx->opts.height = opts->height;
    ctx->opts.fullscreen = opts->fullscreen;
    ctx->opts.borderless = opts->borderless;
    ctx->pending_url = kc_wvw_strdup(opts->url);
    if (!ctx->opts.url || !ctx->opts.title || !ctx->pending_url) {
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
extern "C" int kc_wvw_close(kc_wvw_t *ctx) {
    if (!ctx) {
        return KC_WVW_OK;
    }

    ctx->closing = 1;
    if (ctx->controller) {
        ctx->controller->Close();
    }
    if (ctx->webview) {
        ctx->webview->Release();
        ctx->webview = NULL;
    }
    if (ctx->controller) {
        ctx->controller->Release();
        ctx->controller = NULL;
    }
    if (ctx->environment) {
        ctx->environment->Release();
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

    free(ctx->pending_url);
    free(ctx->signal_handlers);
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
extern "C" int kc_wvw_run(kc_wvw_t *ctx) {
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
extern "C" int kc_wvw_navigate(kc_wvw_t *ctx, const char *url) {
    char *next_url;
    wchar_t *target;
    HRESULT hr;

    if (!ctx || !url) {
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

    hr = ctx->webview->Navigate(target);
    free(target);
    return FAILED(hr) ? KC_WVW_ERROR : KC_WVW_OK;
}
