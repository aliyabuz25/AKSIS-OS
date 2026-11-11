# SHENLONG Ultralight bridge

This is an optional host-side integration for the real Ultralight SDK. The
freestanding kernel must not link C++/Ultralight directly. A future user-space
or host bridge can call the small C ABI in `ultralight_bridge.h` and copy the
rendered surface into a SHENLONG window.

Build with the official SDK installed:

```sh
cmake -S ultralight -B build/ultralight -DULTRALIGHT_SDK_ROOT=/path/to/ultralight
cmake --build build/ultralight --config Release
```

The bridge uses the real Ultralight `Renderer` and `View` APIs, including
`LoadHTML`, `Update`, `Render`, and `bitmap()`. It is intentionally optional so
the kernel's freestanding C build remains deterministic.
