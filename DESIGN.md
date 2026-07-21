# wvw.c Design

## Purpose

`wvw.c` opens one native application window and delegates page rendering to the
WebView already associated with the target platform. It provides a narrow C API
for lifecycle, navigation, selected host-window modes, optional tray behavior,
and an opt-in native bridge.

## Architecture

The CLI builds options, opens one context, optionally enables demo bridge methods
and a tray icon, then enters the native event loop. The library deep-copies
configuration and owns all native objects.

The source files have fixed responsibilities:

- `src/wvw.c` owns CLI behavior and demo-only bridge callbacks;
- `src/libwvw.c` owns Linux and Windows implementations;
- `src/macos.m` owns the current Apple implementation;
- `src/libwvw.h` defines the common public contract;
- `src/test.c` contains all tests.

## Window Options

The context owns copied URL, title, and background strings plus width, height,
fullscreen, borderless, topmost, click-through, and no-focus flags. Environment
values load before CLI flags replace them.

Default dimensions are selected by each backend when configured dimensions are
not positive. Host modes map directly to native window features and are not
guaranteed to compose identically across window managers or operating systems.

Background accepts `RRGGBB` or `AARRGGBB`. Windows WebView2 accepts only fully
transparent or fully opaque alpha. Transparent host mode also depends on native
compositor support and transparent page CSS.

## Linux Backend

Linux uses GTK 3 for the host window and WebKitGTK for page rendering and the
native event loop. It applies window decoration, fullscreen, topmost,
click-through, no-focus, transparent visual, and tray behavior through GTK and
related platform facilities where available.

WebKitGTK owns browser process behavior, network access, web storage, certificate
handling, and rendering. Those are not reimplemented by `wvw`.

## Windows Backend

Windows creates one Win32 window and initializes WebView2 asynchronously through
COM completion handlers. The controller fills the client area and navigation may
be queued until initialization reaches ready state.

`WebView2Loader.dll` is loaded only from beside the executable. The Evergreen
Runtime must already be installed. WebView2 user data is placed under
`LOCALAPPDATA\KaisarCode\wvw\WebView2`.

The backend owns COM initialization, environment, controller, WebView, callback
handlers, native window, brush, loader, tray resources, and copied menu items.
Close must release them in lifecycle-safe order.

## macOS Backend

macOS uses Cocoa and WKWebView from `src/macos.m`. It creates a titled resizable
window, applies fullscreen, borderless, and topmost modes, and runs `NSApp`.

The current backend does not implement NativeBridge despite returning success
from bridge enablement. Tray functions are also no-op success stubs. Several
other host modes and cleanup paths do not match the primary backends. This is an
explicit implementation gap, not portable feature behavior.

## NativeBridge

The bridge is off until `kc_wvw_enable_bridge()` receives at least one valid
method, a callback, and scheme allowances. Method names must be safe JavaScript
identifiers and are deep-copied into context state.

Injected JavaScript creates `window.NativeBridge`, assigns one function per
allowed method, generates string request IDs, tracks callbacks in page memory,
and exchanges serialized request and response objects with native code.

Native responses have one of these shapes:

```text
{"id":"...","ok":true,"result":<json>}
{"id":"...","ok":false,"error":<json>}
```

Unsolicited native events are dispatched as `CustomEvent("nativebridge")` with
the parsed JSON value as event detail.

## Bridge Authority

Four methods are implemented by the bridge core without application whitelist
entries: hide, show, minimize, and quit. Application methods are dispatched only
after exact whitelist lookup.

The callback receives borrowed method and params strings and may return one
allocated serialized JSON value. The library wraps and frees that result. A NULL
success result becomes JSON `null`; a NULL failure result becomes a standard
error object.

Bridge messages are bounded to 65,536 bytes in the primary implementations.
The extraction code recognizes the expected generated request shape rather than
implementing complete JSON grammar.

## Origin Policy

Bridge-enabled navigation is accepted when it matches one of these explicit
trust sources:

- `file://` when `allow_file` is set;
- `data:` when `allow_data` is set;
- localhost HTTP or HTTPS when `allow_localhost` is set;
- the exact extracted origin of the initial URL;
- an exact origin token in `TRUSTED_ORIGINS`.

Other navigation is blocked while the bridge is active. Origin extraction is a
small textual operation, not a standards-complete URL parser. It must not be
treated as a general browser security policy.

## Tray and Window Control

Linux and Windows can create one tray icon associated with the window. Closing
may hide the host instead of terminating when tray mode is active. Left-click
toggles visibility; right-click exposes default or copied custom menu items.

The action `quit` is handled natively. Other custom actions are emitted to page
content as bridge events. Tray item labels and action strings are copied into the
context.

Hide, show, and minimize are direct native operations. They do not define an
application navigation or document lifecycle.

## Event Loop and Stop

`kc_wvw_run()` enters the backend's native UI loop and returns when that loop
ends. Stop or close requests arrange loop termination through native mechanisms.
UI operations are intended for the owning UI thread.

## Persistence and Network

`wvw` itself provides no server and performs no network protocol work. A loaded
page may access networks according to WebView policy. Browser engines may persist
cookies, caches, local storage, and other profile data independently.

Local-first operation uses `file://` content with the bridge allowances chosen
by the application. Remote content is optional and must not become a mandatory
project dependency.

## Non-Goals

The project does not provide a browser UI, tabs, downloads, bookmarks, password
management, extension APIs, browser synchronization, bundled rendering engine,
web framework, application server, automatic updater, runtime installer,
identity platform, telemetry service, remote fleet manager, or plugin market.

These exclusions define the tool rather than an unfinished roadmap.

## Change Criteria

A change must solve a concrete host-window or bridge problem, preserve explicit
native ownership, define UI-thread and asynchronous lifecycle effects, maintain
strict origin and method authority, bound messages, state platform support
truthfully, and avoid expanding into browser or desktop-framework scope.

Changes justified mainly by Electron parity, enterprise desktop management,
hosted content platforms, or generalized browser features should be rejected.

## Core Invariants

The project is defined by one native window, one platform WebView, explicit
options, a blocking native loop, an opt-in whitelisted bridge, restricted trusted
navigation, direct native tray and visibility controls, and no bundled browser
or hosted infrastructure.
