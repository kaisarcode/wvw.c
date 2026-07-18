# AGENTS.md

## Project Context

`wvw.c` is a small C library and CLI for opening one native window containing
the platform WebView.

It is an application-surface wrapper, not a browser, Electron replacement,
desktop framework, remote application platform, or enterprise UI runtime.

Read `README.md` and `DESIGN.md` before modifying the project.

## Core Invariants

- One context owns one native window and one embedded WebView.
- Linux uses GTK 3 and WebKitGTK.
- Windows uses Win32 and the WebView2 Evergreen Runtime.
- macOS uses Cocoa and WKWebView through the existing Objective-C backend.
- The initial URL, title, size, background, and window modes remain explicit.
- The native event loop remains platform-owned and blocking.
- NativeBridge is disabled by default.
- Bridge methods are copied from a fixed explicit whitelist.
- Bridge navigation is confined to trusted origins and explicit scheme allowances.
- Bridge requests and responses remain serialized JSON values.
- Browser installation and application packaging remain outside the library.
- No hosted control plane, user account, or network service is required by wvw.

## Browser Boundary

The embedded engine owns HTML, CSS, JavaScript, networking, cookies, storage,
certificates, accessibility, and page rendering. `wvw` owns only the host window,
navigation request, selected window modes, bridge, and optional tray integration.

Do not add tabs, address bars, bookmarks, downloads, password storage, browser
extensions, profile synchronization, ad blocking, developer platforms, update
services, or generic browser policy.

Persistent browser state created by WebView2 or WebKit is native engine state,
not a `wvw` database. Do not silently relocate, upload, synchronize, or inspect
it.

## Bridge Security Boundary

Treat all page messages as untrusted. Preserve method-name validation, explicit
whitelisting, the 65,536-byte message bound where implemented, callback return
ownership, and valid serialized JSON requirements.

The built-in window methods `hideWindow`, `showWindow`, `minimizeWindow`, and
`quit` are available whenever the bridge is active, outside the application
method whitelist. Changes must account for their authority explicitly.

Trusted navigation currently includes the initial URL origin, origins listed in
`TRUSTED_ORIGINS`, and only those file, data, or localhost classes enabled in
bridge options. Do not broaden trust through suffix matching, redirects, wildcard
origins, remote configuration, or convenience defaults.

The bridge JSON extraction is deliberately small and not a general JSON parser.
Security changes must test escaping, nesting, malformed values, duplicate keys,
oversized input, identifier injection, callback output, and origin parsing.

Never enable the bridge for arbitrary remote content by default.

## Platform Truth

Do not hide platform differences behind the common API.

Linux and Windows implement the primary bridge, navigation restriction, window,
and tray paths in `libwvw.c`. Windows requires a matching
`WebView2Loader.dll` beside the executable and an installed WebView2 Runtime.
The library does not download either dependency.

The current macOS backend in `src/macos.m` opens WKWebView but bridge enablement
and tray functions are stubs returning success. Its click-through, no-focus,
background, signal, cleanup, and lifecycle behavior are not equivalent to Linux
or Windows. Do not claim parity until runtime behavior exists and is tested.

Transparent host mode and tray behavior are best-effort platform features, not
portable rendering guarantees.

## Lifecycle and Threading

Open deep-copies effective options into the context. Run enters the native event
loop. Navigate, bridge event delivery, tray operations, visibility changes, and
close interact with native UI objects and must obey the platform UI thread.

Keep close idempotence and asynchronous backend initialization explicit. On
Windows, WebView2 environment and controller creation use COM callbacks and may
hold context references during initialization.

Do not add hidden worker pools, cross-thread UI mutation, resident agents, or a
generic task dispatcher without a concrete requirement.

## Public API and Ownership

Treat `src/libwvw.h` as a compatibility boundary. Preserve option ownership,
callback signatures, bridge result ownership, tray item copying, event name,
return codes, and native loop behavior unless explicitly instructed otherwise.

The bridge callback allocates `*result_json`; the library consumes and frees it.
Bridge methods are copied. Callback userdata is borrowed. Posted event JSON is
borrowed for the call.

## Source Layout

Preserve the existing source set:

- `src/wvw.c` for CLI parsing and demo bridge methods;
- `src/libwvw.c` for Linux and Windows backends plus shared behavior;
- `src/macos.m` for the existing Apple backend;
- `src/libwvw.h` for the public API;
- `src/test.c` for all tests, including bridge, lifecycle, platform, and
    integration cases.

Do not create additional source, header, backend, bridge, tray, platform, or test
files. Extend only these existing files.

## Forbidden Default Recommendations

Do not add Electron, Chromium bundles, browser frameworks, web servers, hosted
content platforms, automatic runtime downloads, remote debugging services,
extension systems, OAuth, SSO, accounts, tenant models, telemetry, analytics,
distributed tracing, dashboards, fleet management, cloud APIs, plugin ecosystems,
or application marketplaces.

Do not justify changes through enterprise readiness, browser parity,
hypothetical scale, managed operation, or platform growth.

## Testing

All tests remain in `src/test.c`. Behavioral changes should cover option copying,
native open and close, event-loop exit, missing runtimes, navigation before and
after initialization, exact origin rules, every bridge allowance, malformed and
oversized messages, method validation, callback ownership, valid and invalid JSON,
built-in authority, tray menu actions, background alpha, visibility modes,
transparent hosts, cleanup, and actual Linux, Windows, and macOS behavior.

Headless argument tests do not establish WebView runtime correctness. Do not
claim platform support without operator-visible window and bridge testing.

## Build and Completion

For documentation-only changes run `kcs .`. For behavior changes use the
repository build and tests without cleaning unless authorized.

A change is complete when native lifecycle, bridge trust, platform behavior,
ownership, runtime dependencies, tests, and documentation agree.

The goal is one small, explicit native WebView window.
