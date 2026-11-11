# SHENLONG 3.1 desktop update

The desktop is a native C shell, with a Windows 10 style taskbar and Start menu.
It is not Windows and does not implement DirectX or hardware video decoding.

## What changed

- Shared painting and hit-test rectangles for Start, search, pinned applications,
  taskbar buttons, and the minimize/maximize/close controls.
- Left application list and two-column pinned tiles, keyboard search, Escape to
  dismiss popups, desktop double-click, and keyboard-accessible context actions.
- Cached vector icons with 2× supersampling at the sizes used by the desktop.
  This is procedural vector drawing, not a general SVG file parser.
- Move/fade window transitions preserve text pixels. Window buffers retain their
  allocation when maximizing/restoring. `gpu_fill` respects the allocation stride.
- Taskbar repaints depend on its own state; clock updates happen once per minute.
  The compositor has a 100 Hz pacing limit. This is not a measured frame rate.
- Jade matches complete CSS class tokens, wraps paragraph text and narrow flex
  rows, and keeps headings separate from card backgrounds. The supported HTML/CSS
  subset remains limited; it is not a full browser engine.
- `tools/embed-ui.py` makes `assets/ui` the source of the pages installed in a
  fresh guest. Editing those files triggers regeneration and recompilation.
- The UTM package uses a read-only CD image and a separate blank data disk.
  Existing UTM bundles are not replaced by this release.

## Verification

Run `make test-win10-ui` on the current macOS/UTM development machine.

`tools/test-html-ui.c` runs with AddressSanitizer and UndefinedBehaviorSanitizer.
It checks real page parsing/layout at 320, 480, 800 and 976 pixels, link hit tests,
surface clipping and the small media icon. `tools/test-compositor.c` compares
dirty composition against a full-frame reference at 24 and 32 bpp, including
stride, transparency, movement, hidden windows and the final paced update.

`tools/test-win10-desktop.py` sends real PS/2 input through QMP at 1920×1080.
It exercises Start/search, repeated maximize/restore, minimize, dragging, show
desktop, context actions, calendar, every application and the About page.
Pass `--bios` for BIOS or `--utm build/out/SHENLONG-v3.1-Win10.utm` to use the
packaged ISO and corresponding devices. VM data disks are opened as snapshots.
Screenshots and JSON results, including the tested ISO SHA-256, go to `build/out`.

Physical frame rate, hardware acceleration and arbitrary HD video formats are
not established by these checks. Resolution is selected at boot. Context actions
provide display information and wallpaper cycling; live resolution changes and a
wallpaper file picker are not implemented.

In the tested q35 configuration, Files reports a RAM disk. The presence of a blank
data image in the package does not establish persistent storage support with this
controller; persistence was not validated by the desktop tests.

The CD image configuration follows UTM's [drive enum](https://github.com/utmapp/UTM/blob/main/Configuration/QEMUConstant.swift)
and [QEMU argument generation](https://github.com/utmapp/UTM/blob/main/Configuration/UTMQemuConfiguration%2BArguments.swift).
