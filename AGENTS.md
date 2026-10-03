# ergo-active – Copilot Workspace Overview

ergo-active is a native Win32 C++ desktop application that monitors keyboard and mouse activity and reminds the user to take regular breaks. It is a single executable, built with CMake and Ninja through the vendored dd driver.

## Project

| Project | Output | Purpose |
|---------|--------|---------|
| `CMakeLists.txt` | `exe/ergo-active-64[d].exe` | Main GUI application |

Input activity is detected via the Win32 `GetLastInputInfo` API — no hook DLL or driver is required.

## Key Source Files

Files follow a naming convention: `data-*` for data/logic, `ui-*` for presentation.

### Data layer
- **data.h** – `daily_stats` persists per-day statistics to `%APPDATA%\ergo-active\history.csv`; `usage_data` owns the rolling activity window (persisted to `%APPDATA%\ergo-active\usage.dat`) and detects breaks, micro-pauses and the 20-20-20 interval. `TicksPerMinute` is the shared sampling rate.

### UI layer (`ui-*`)
- **ui.h** – GDI drawing helpers and RAII wrappers: `scoped_delete_object`, `scoped_select_object`, `buffered_paint_surface`, `rect_f`, `font_spec`, color blending, DPI helpers, dark caption support, and `draw_context` (a GDI-object-caching drawing surface). All drawing goes through `draw_context`.
- **ui-frame.h** – `main_frame` class; owns the dark-themed dashboard UI with stat cards, activity graph, usage pie chart, delay slider, tray icon with color-coded urgency, break/eye-strain reminders, posture tips, and daily history integration.
- **ui-task-bar-icon.h** – `task_bar_icon` class; tray icon management with dynamic color-coded urgency icons (green/yellow/red), context menu, balloon tips, tooltip updates, and `TaskbarCreated` restart recovery.

### Infrastructure
- **main.cpp** – `wWinMain` entry point: single-instance guard, common-controls init, message loop. Also holds the exit/hide dialog procedure and the out-of-line `draw_context::draw_usage_graph` / `draw_context::draw_pie_chart` renderers.
- **win.h** – Common header; sets up Unicode, Win32 target version macros, includes Windows & C++ standard library headers, links `comctl32`, `dwmapi`, `Wtsapi32`.
- **resource.h** – Resource identifier constants.
- **ergo-active.rc** – Resource script (icon, tray menu, exit dialog, version info, string table). All resources live in a single U.S. English language block.
- **res/ergo-active.exe.manifest** – Application manifest enabling PerMonitorV2 DPI awareness, common controls v6, long paths and UTF-8. `CMakeLists.txt` lists it as a source, so the linker embeds it. Do **not** also embed it from the `.rc` (that produces `CVT1100: duplicate resource`), and do not link with `/MANIFEST:NO`, which drops it entirely — the first CMake build did exactly that, leaving the app DPI-unaware and on version 5 common controls, and nothing failed.

## Build

- Built with CMake and Ninja through the vendored dd driver (`dd.ps1`, PowerShell 7.4+), which sets up the MSVC environment itself.
- C++ standard: **C++20**; CMake compiles at `/W3`, and the tree is warning-clean — keep it that way.
- Configurations: Debug and Release, x64 only.
- `.\dd.ps1 build` builds both; `.\dd.ps1 launch` builds Release and starts it; `.\dd.ps1 test --label ergo-active` builds both and runs CTest, which is a build smoke check only — verify behaviour by running the app.
- Close any running `ergo-active-*` before building — the linker cannot overwrite a running exe, and the single-instance mutex is shared across build flavours, so a stray Debug copy would swallow the new launch.
- Outputs land in `exe/` as `ergo-active-64.exe` and `ergo-active-64d.exe`.
- Verify resource or manifest changes from a clean build (`.\dd.ps1 clean --yes`, then build); an incremental build can hide resource-linking errors.
- CI (`.github/workflows/build.yml`) runs `dd.ps1 test` — both configurations plus a window smoke check — from a clean checkout, so anything that only reproduces on a full build will break it.

## Conventions

- Header-only style for most app code; implementation lives in `.h` files.
- Dark color scheme constants defined in `main_frame` (`BackgroundColor`, `SurfaceColor`, `AccentColor`, `TextColor`, `DimTextColor`, `GraphBorderColor`, `GreenColor`, `YellowColor`, `RedColor`).
- Per-monitor DPI awareness (PerMonitorV2) via app manifest; runtime DPI helpers in `ui.h`.
- Delay setting persisted in `HKCU\Software\ergo-active`.
- Activity polling uses `GetLastInputInfo` on a 10-second timer tick (`60s / TicksPerMinute`).
- Child-control geometry is produced by `main_frame::measure()` and applied by `layout_controls()` from `WM_SIZE`/`WM_DPICHANGED`. Never move child windows from `WM_PAINT`.
- A locked session counts as time away from the keyboard, so it accrues toward a break.
- Breaks and micro-pauses are detected by `usage_data::step()` and reported to `daily_stats` via its returned `step_events` — never inferred from a notification being shown.
- Usage data is flushed every 5 minutes as well as on exit and `WM_ENDSESSION`.
- 20-20-20 eye reminder fires every 20 minutes of continuous activity, and yields to an overdue break warning.
- Tray icon dynamically changes color: green (OK) → yellow (approaching limit) → red (overdue). `NIF_SHOWTIP` is required alongside `NOTIFYICON_VERSION_4` or the tooltip is suppressed.
- Only one instance runs; a second launch broadcasts `main_frame::show_window_message()` and exits. (`FindWindow` by class name is not reliable across processes here — use the broadcast.)
- Text is drawn with `ETO_CLIPPED`, so a rect narrower than the string silently truncates it (right-aligned text loses its *left* side). Size text rects with `draw_context::measure_text_width` rather than hardcoded widths.
