# wvw.c - Native WebView Window Wrapper

`wvw.c` is a small C library and CLI for opening one native window that hosts a WebView.

Backends:

- Linux uses WebKitGTK.
- Windows uses Microsoft Edge WebView2.

---

## CLI

### Examples

Open one URL with default size:

```bash
./bin/x86_64/linux/wvw --url https://example.com
```

Open one URL with explicit title and size:

```bash
./bin/x86_64/linux/wvw --url https://example.com --title "Example" --width 1280 --height 720
```

Open one URL with a non-white startup background:

```bash
./bin/x86_64/linux/wvw --url https://example.com --background 111111
```

Start in borderless mode:

```bash
./bin/x86_64/linux/wvw --url https://example.com --borderless
```

Start in fullscreen mode:

```bash
./bin/x86_64/linux/wvw --url https://example.com --fullscreen
```

---

### Parameters

| Command/Flag | Description |
| :--- | :--- |
| `--url <url>` | Set the initial URL. |
| `--title <title>` | Set the native window title. |
| `--background <hex>` | Set the WebView background color using `RRGGBB` or `AARRGGBB`. |
| `--width <px>` | Set the initial window width. |
| `--height <px>` | Set the initial window height. |
| `--fullscreen` | Open the window in fullscreen mode. |
| `--borderless` | Open the window without native decorations. |
| `-h`, `--help` | Show help and usage. |
| `-v`, `--version` | Show build version. |

---

## Environment

| Variable | Description |
| :--- | :--- |
| `KC_WVW_URL` | Default URL when `--url` is omitted. |
| `KC_WVW_TITLE` | Default window title. |
| `KC_WVW_BACKGROUND` | Default WebView background color in `RRGGBB` or `AARRGGBB`. |
| `KC_WVW_WIDTH` | Default window width. |
| `KC_WVW_HEIGHT` | Default window height. |
| `KC_WVW_FULLSCREEN` | Default fullscreen flag (`0` or `1`). |
| `KC_WVW_BORDERLESS` | Default borderless flag (`0` or `1`). |
| `KC_WVW_BROWSER_ARGS` | Windows WebView2 browser arguments. |

---

## Public API

```c
#include "wvw.h"

kc_wvw_options_t opts = kc_wvw_options_default();
opts.url = strdup("https://example.com");

kc_wvw_t *ctx = NULL;
kc_wvw_open(&ctx, &opts);
kc_wvw_run(ctx);
kc_wvw_close(ctx);
kc_wvw_options_free(&opts);
```

## NativeBridge

`wvw` can inject one native application bridge into trusted local content.

JavaScript request surface:

```js
window.NativeBridge.MethodName(params, function (err, result) {
    if (err) {
        return;
    }
});
```

JavaScript event surface:

```js
window.addEventListener("nativebridge", function (e) {
    const message = e.detail;
});
```

Bridge rules:

- `window.NativeBridge` is injected by native code.
- Only whitelisted methods exist.
- The bridge is disabled by default.
- Remote navigation is blocked when the bridge is active.
- `localhost` is denied unless the application enables it explicitly.

### Bridge API

```c
typedef int (*kc_wvw_bridge_callback_t)(
kc_wvw_t *ctx,
const char *method,
const char *params_json,
char **result_json,
void *userdata
);

typedef struct {
    const char **methods;
    int method_count;
    kc_wvw_bridge_callback_t callback;
    void *userdata;
    int allow_file;
    int allow_data;
    int allow_localhost;
} kc_wvw_bridge_options_t;

int kc_wvw_enable_bridge(kc_wvw_t *ctx, const kc_wvw_bridge_options_t *opts);
int kc_wvw_post_bridge_event(kc_wvw_t *ctx, const char *json);
```

Callback contract:

- `method` is one of the registered bridge methods.
- `params_json` is the serialized JSON fragment received from JavaScript.
- Return `KC_WVW_OK` for success.
- Set `*result_json` to one serialized JSON value when needed.
- Return `KC_WVW_ERROR` for failure.
- On failure, `*result_json` may contain one serialized JSON error object.

`kc_wvw_post_bridge_event()` sends one serialized JSON payload to the current page as the `nativebridge` event detail.

### Example

```c
static int app_bridge(
kc_wvw_t *ctx,
const char *method,
const char *params_json,
char **result_json,
void *userdata
) {
    (void)ctx;
    (void)params_json;
    (void)userdata;

    if (strcmp(method, "GetVersion") == 0) {
        *result_json = strdup("{\"version\":1}");
        return KC_WVW_OK;
    }

    *result_json = strdup("{\"code\":\"UNKNOWN_METHOD\",\"message\":\"Bridge method is not implemented.\"}");
    return KC_WVW_ERROR;
}

int main(void) {
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bridge;
    kc_wvw_t *ctx;
    const char *methods[] = { "GetVersion" };

    opts = kc_wvw_options_default();
    opts.url = strdup("file:///tmp/app.html");
    ctx = NULL;
    kc_wvw_open(&ctx, &opts);

    memset(&bridge, 0, sizeof(bridge));
    bridge.methods = methods;
    bridge.method_count = 1;
    bridge.callback = app_bridge;
    bridge.allow_file = 1;
    kc_wvw_enable_bridge(ctx, &bridge);

    kc_wvw_run(ctx);
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return 0;
}
```

---

## Lifecycle

- `kc_wvw_options_default()` creates default window options.
- `kc_wvw_options_load_env()` applies environment overrides.
- `kc_wvw_open()` creates the native window and embedded WebView.
- `kc_wvw_run()` starts the native event loop.
- `kc_wvw_navigate()` loads a new URL.
- `kc_wvw_enable_bridge()` injects `window.NativeBridge` into trusted pages.
- `kc_wvw_post_bridge_event()` emits one `nativebridge` event into the page.
- `kc_wvw_close()` releases the window, WebView, and associated resources.

Color input accepts `RRGGBB` and `AARRGGBB`. On Windows, alpha must be `00` or `FF` because WebView2 does not support semi-transparent startup colors.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the supported targets.

```bash
make clean && make
```

### Supported targets

```bash
make all
make x86_64/linux
make x86_64/windows
```

The Windows target builds the backend as C and uses the official Microsoft Edge WebView2 Win32 SDK headers stored under `lib/webview2/include/`.

`make x86_64/windows` copies the architecture-matched `WebView2Loader.dll` from `lib/webview2/bin/x86_64/` to `bin/x86_64/windows/` beside `wvw.exe`.

The current Windows cross-build path uses MinGW-w64:

```bash
make x86_64/windows
```

Native MSVC builds are supported by the CMake project when the same WebView2 SDK files are available, but this repository's default workflow is the Make target above.

---

## Windows Runtime

Windows 10 and Windows 11 require the Microsoft Edge WebView2 Evergreen Runtime installed on the target machine.

The library does not install or download the runtime. Runtime installation belongs to application packaging.

If the runtime is missing, `kc_wvw_open()` returns `KC_WVW_ERROR` and the backend prints:

```text
wvw: Microsoft Edge WebView2 Runtime is required
```

Windows packages must include these files together:

- `wvw.exe`
- `libwvw.dll`
- `WebView2Loader.dll`

The loader must be the matching architecture and must remain beside `wvw.exe`. The backend does not search arbitrary directories.

WebView2 stores persistent browser state under:

```text
%LOCALAPPDATA%\KaisarCode\wvw\WebView2
```

Wine can run the Windows backend when the prefix provides a working WebView2 rendering path. `KC_WVW_BROWSER_ARGS` is available for explicit WebView2 diagnostics and runtime experiments.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com). Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
