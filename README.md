# wvw.c - Native WebView Window Wrapper

`wvw.c` is a small C library and CLI for opening one native window that hosts a WebView.

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
| `KC_WVW_WIDTH` | Default window width. |
| `KC_WVW_HEIGHT` | Default window height. |
| `KC_WVW_FULLSCREEN` | Default fullscreen flag (`0` or `1`). |
| `KC_WVW_BORDERLESS` | Default borderless flag (`0` or `1`). |

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

---

## Lifecycle

- `kc_wvw_options_default()` creates default window options.
- `kc_wvw_options_load_env()` applies environment overrides.
- `kc_wvw_open()` creates the native window and embedded WebView.
- `kc_wvw_run()` starts the native event loop.
- `kc_wvw_navigate()` loads a new URL.
- `kc_wvw_close()` releases the window, WebView, and associated resources.

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

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com). Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
