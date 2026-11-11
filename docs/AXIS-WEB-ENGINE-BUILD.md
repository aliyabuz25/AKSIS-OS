# AXIS Web Engine source integration

The real upstream sources are now vendored under `vendor/axis-web/`:

- `lexbor/` — HTML, DOM, CSS, selectors, URL and encoding modules.
- `quickjs/` — embeddable JavaScript runtime.

They are intentionally not linked into the freestanding kernel yet. Both
projects assume a hosted C environment during their normal build, so linking
them directly into the kernel would create an unverified and non-bootable
binary. Use:

```sh
make axis-web-deps
```

to build the upstream libraries in isolation first. The next integration gate
is an AXIS adapter compiled against those libraries with explicit allocator,
resource-loader, timer, and event-loop callbacks. Only after that adapter has a
host smoke test should its freestanding subset be added to the kernel.
