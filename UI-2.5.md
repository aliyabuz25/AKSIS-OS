# SHENLONG 2.5

2.5 uses a single light palette: porcelain backgrounds, slate text, blue selection,
consistent spacing and rounded surfaces. Terminal and video retain dark content
areas. App and file icons are locally drawn and antialiased; no downloaded icon
pack or desktop GUI dependency is required.

- Desktop: retained, resolution-independent landscape and two useful shortcuts.
- Dock: continuous hover lift/size, app labels and running indicators.
- Windows: centered titles, traffic lights, cached shadows and a dock-origin
  funnel transition for opening/restoring/minimizing. Window content buffers reserve
  the screen capacity once, so maximizing cannot exceed the allocation. Shadows
  reserve only restored-window dimensions and are hidden while maximized.
- Launcher: two-column app grid with search and keyboard navigation.
- Calendar: reversible opacity/position transition and a six-week grid that fits.
- Files: consistent sidebar, safe root-parent navigation, clipped previews using
  the VFS data accessor, file types/sizes and up/down scrolling.
- System/network: aligned information rows and concise connection state.
- Jade: light chrome, rounded address field and matching bookmark hit regions.
  It remains the project's native text/link reader, not a complete Chromium port.
- Media: library sidebar, letterboxed video, distinct transport and volume groups,
  seek/volume dragging, Space to pause, arrows to seek, M to mute and F to maximize.
  The decoder remains MPEG-1; MP4 support is not implied.

Shared allocation-free drawing helpers live in `kernel/ui_paint.h`. The compositor
retains its 100 Hz presentation target and damaged-region rendering. This is a
configured upper cadence, **not measured physical-display FPS**; UTM emulation,
video decode cost and host refresh rate still determine observed smoothness.
The desktop performs no wallpaper animation and its minute clock does not repaint
the dock each second.

## Verification and artifacts

```sh
make -j4 iso build/out/utm-qemu-runner build/out/test-media build/out/test-compositor
UBSAN_OPTIONS=halt_on_error=1 build/out/test-media
build/out/test-compositor
build/out/test-compositor 24
python3 tools/test-ui-v25.py
python3 tools/test-ui-v25.py --bios
```

The headless smoke test uses the installed UTM QEMU library, a separate 1 GiB test
VM and real PS/2 input/framebuffer reads. It leaves existing user VMs/disks alone.
Screenshots and reports are in `build/out/v25-*.png` / `v25-result.json` (and the
`v25-bios-*` equivalents). The UTM package uses 6 GiB, a fresh 64 MiB data disk and
the self-contained hybrid ISO. `make utm` creates `SHENLONG-v2.5.utm` and refuses
to overwrite an existing bundle; previous releases are left intact.
