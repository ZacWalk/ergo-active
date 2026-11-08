# ergo-active – Copilot Workspace Overview

ergo-active is a native Windows C++20 break reminder built on libdesktop's
`core`, `platform`, `render` and `ui` libraries. It monitors keyboard and mouse
activity, keeps the dashboard in the notification area, and reminds the user
to take regular breaks.

## Project

| Project | Output | Purpose |
|---------|--------|---------|
| `CMakeLists.txt` | `exe/ergo-active-64[d].exe` | Main GUI application |

`cmake/dd-dependencies.json` records the libdesktop revision used by the
sibling checkout or fetched dependency. CMake declares the application with
`libdesktop_add_app` and links `libdesktop::ui`.

## Key Source Files

### Data layer
- **data.h** – Platform-neutral `daily_stats` and `usage_data`. The model keeps
  the existing `%APPDATA%\ergo-active\history.csv` and `usage.dat` formats,
  rolling-window aging, micro-pauses, break detection, scoring, and save
  cadence through the small `activity_store` contract.
- **dashboard-layout.h** and **dashboard-policy.h** – Headless layout,
  numeric delay-editor, input classification, timer-aging, urgency, and
  reminder policy seams.

### UI layer
- **ui-frame.h** – `main_frame`, a `ui::element_host` for the application frame.
  Its element tree contains the drawn menu bar, dashboard canvas, `ui::trackbar`
  and `ui::edit`; charts and stat cards paint through `render::draw_context`.
  The frame owns no native window or Win32 drawing handle.
- **app-services.h** – Neutral activity, session, settings and tray contracts
  used by the model/presentation boundary.
- **desktop-services.h** – Thin adapters over libdesktop's roaming app-data,
  atomic file publication, settings, clipboard and tray services.
- **win-services.h** – The explicit Windows monitoring adapter only:
  `GetLastInputInfo`, locked-desktop polling, and session state. It contains no
  tray, window, registry or file implementation.

### Infrastructure
- **main.cpp** – Owns `main`, defines the `ui::app` lifecycle, single-instance
  signaling, frame creation, frame ticks and teardown, and `platform::run` drives
  the lifecycle. `libdesktop_add_app` supplies the manifest, icon and version
  resources.
- **res/ergo-active.ico** – The application icon supplied to CMake.

## Build

- Built with CMake/Ninja through the vendored dd driver (`dd.ps1`,
  PowerShell 7.4+), which sets up MSVC.
- `\.\dd.ps1 build` builds both configurations.
- `\.\dd.ps1 test --label ergo-active` builds both configurations and runs the
  headless model, persistence, dashboard/render-policy and manifest checks. No
  test opens the dashboard or launches the visible app.
- Keep `CMAKE_BUILD_PARALLEL_LEVEL=2` for focused verification. Do not launch
  the dashboard or disturb a running user instance while building/testing.

## Conventions and preserved behavior

- Keep the dashboard charts, stat cards, slider, numeric editor, keyboard
  focus behavior, menus, tray policy and reminders; do not replace them with a
  generic list or placeholder.
- `dashboard_layout::measure()` is the single source of control geometry and
  runs from the ui frame host's layout callback and DPI-change layout.
- The app samples `GetLastInputInfo` every ten seconds through the isolated
  Windows adapter. A locked session is away from the keyboard.
- Breaks and micro-pauses are detected by `usage_data::step()` and reported to
  `daily_stats` through `step_events`, never inferred from a notification.
- Usage data ages missed timer periods as idle ticks, including restart and
  sleep gaps. Breaks completed in a gap count only when the gap began today.
- Usage/history flushes every five minutes, on end-session, on exit and during
  teardown. The 20-20-20 reminder yields to an overdue break warning.
- Tray urgency remains green → yellow → red; tooltip updates, balloon tips,
  keyboard selection/context-menu anchor and `TaskbarCreated` recovery remain
  platform-service responsibilities.
- `platform::try_lock_named_instance("Local\\ergo-active-single-instance")`
  preserves the existing mutex identity. A second process sends the registered
  `ergo-active-show-window` signal and exits; it does not use window heuristics.
- The neutral data/presentation files must not include Windows headers. Generic
  GUI, tray, settings and file behavior belongs in libdesktop; only domain
  activity/session observation belongs in `win-services.h` behind a contract.
