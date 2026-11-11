# AXIS Web Engine decision

## Decision

AXIS will use a staged, native C web runtime:

```text
Lexbor HTML/DOM/CSS/URL/encoding
             ↓
AXIS DOM adapter + CSS cascade
             ↓
AXIS layout / paint / event loop
             ↓
AXIS GPU surfaces + compositor
             ↕
QuickJS ES2020 runtime + AXIS Web APIs
```

The existing `kernel/html_ui.c` remains the compatibility fallback until the
new path passes the same page and interaction tests. No Linux, POSIX runtime,
GTK, X11, Wayland, C++ runtime, or host browser service is part of this plan.

## Why this combination

Lexbor is pure C99 with no external dependencies and provides standards-based
HTML, DOM, CSS, selectors, URL, encoding, and Unicode modules. It does not
provide a complete layout or font engine, so AXIS must own those layers.

QuickJS is small and embeddable C and supports ES2020, modules, async
generators, proxies, and BigInt. It is a JavaScript runtime, not a browser;
`window`, `document`, events, fetch, storage, timers, workers, and WebSocket
must be implemented as AXIS host objects.

litehtml is not selected because it is C++/STL based, has no JavaScript
runtime, and is explicitly intended for lightweight HTML/CSS rather than a
full browser. NetSurf is not selected because its JavaScript integration is
not a suitable foundation for modern React applications.

## Current compatibility audit

| Area | Current AXIS state | Required before React-class apps |
|---|---|---|
| HTML/DOM | Custom parser in `kernel/html_ui.c` | Lexbor DOM adapter |
| CSS | Class shortcuts and partial inline styles | Lexbor CSSOM, cascade, selectors |
| Layout | Deterministic block/flex subset | Box model, flex, grid, positioning |
| Paint | CPU framebuffer/GPU surfaces | Display lists, clipping, stacking contexts |
| JavaScript | V8-Lite-shaped evaluator | QuickJS runtime and Web APIs |
| Memory | `kmalloc`, bump allocator | arenas, quotas, GC roots, reclamation |
| Threads | No scheduler-backed user threads | event loop first; workers later |
| Files | VFS and POSIX-shaped descriptors | FileSystem/URL resource loader |
| Network | AXIS TCP/TLS client | fetch/XHR/WebSocket adapters |
| Input | PS/2 mouse/keyboard | DOM mouse, pointer, keyboard events |
| GPU | AXIS compositor/framebuffer | paint bridge; WebGL remains unsupported initially |

## Implementation gates

1. Vendor Lexbor as a separately configured C target and compile its HTML,
   DOM, CSS, selector, URL, and encoding modules with `-ffreestanding`.
2. Add `kernel/axis_web/` with a resource loader, DOM ownership rules, CSS
   cascade boundary, layout tree, and event queue. Do not mix this with the
   current parser until the adapter is testable.
3. Add QuickJS only after its libc surface is audited. Replace its standard
   library file/socket helpers with AXIS callbacks; do not expose host `os` or
   filesystem calls to page scripts.
4. Implement the DOM baseline: `window`, `document`, element attributes,
   `querySelector`, event listeners, timers, `requestAnimationFrame`,
   `Promise`, and `console`.
5. Add fetch and storage over the existing AXIS network/VFS services.
6. Switch local system pages behind a feature flag after parser, CSS, JS,
   memory, and input tests pass. Keep the current renderer as rollback.

## Explicit non-goals for the first usable release

WebGL, Web Workers, service workers, IndexedDB, full accessibility tree,
cross-origin isolation, JIT, and browser security sandbox are not safe to claim
until AXIS has user-mode processes, virtual memory isolation, and a scheduler.

## Acceptance tests

The new engine is not considered integrated until it renders and interacts
with pages covering HTML parsing, selectors, CSS cascade, flex layout, DOM
mutation, modules, promises, timers, fetch, canvas 2D, local storage, and
memory teardown under UTM. A successful C/Rust compilation alone is not a
passing browser test.
