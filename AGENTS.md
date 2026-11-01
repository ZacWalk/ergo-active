# ergo-active – Copilot Workspace Overview

ergo-active is a native Windows C++20 break reminder built on the published
`libdesktop` `pf::` core/UI and system services. It monitors keyboard and mouse activity,
keeps the dashboard in the notification area, and reminds the user to take
regular breaks.

## Project

| Project | Output | Purpose |
|---------|--------|---------|
| `CMakeLists.txt` | `exe/ergo-active-64[d].exe` | Main GUI application |

`cmake/dd-dependencies.json` records libdesktop revision
`31aae322af6e0696a9597726705b1121bd31580b`. CMake uses a sibling
`../libdesktop` checkout when present and otherwise fetches that published
revision. The dependency is declared with dd's `application` method so the
sibling override remains explicit and reproducible.

## Key Source Files

### Data layer
- **data.h** – Platform-neutral `daily_stats` and `usage_data`. The model keeps
  the existing `%APPDATA%\ergo-active\history.csv` and `usage.dat` formats,
  rolling-window aging, micro-pauses, break detection, scoring, and save
  cadence through the small `activity_store` contract, returning publication
  diagnostics instead of dropping failed writes.
- **dashboard-layout.h** and **dashboard-policy.h** – Headless layout, numeric
  delay-editor, input classification, timer-aging, urgency, and reminder policy
  seams.

### UI layer
- **ui-frame.h** – `main_frame`, a `pf::frame_reactor` that owns the dark
  dashboard, stat cards, four-hour activity graph, usage pie chart, delay
  slider, numeric edit box, focus/caret policy, menus, reminders and daily
  history. Painting uses `pf::draw_context`; no Win32 type crosses this file.
- **app-services.h** – Neutral activity, session, settings and tray contracts
  used by the model/presentation boundary.
- **desktop-services.h** – Thin adapters over libdesktop's roaming app-data,
  unique-temp/atomic file publication, explicit registry settings, and neutral
  tray service. It retains only ergo's urgency bitmap and policy callbacks.
- **win-services.h** – The explicit Windows monitoring adapter only:
  `GetLastInputInfo`, locked-desktop polling, and session state. It contains no
  tray, window, registry or file implementation.

### Infrastructure
- **main.cpp** – Implements libdesktop's `app_init`, `app_idle` and
  `app_destroy` callbacks. libdesktop owns `WinMain`, the main window, DPI
  manifest, message loop, GDI draw context, focus events, timers and menu
  dispatch.
- **res/ergo-active.ico** – The application icon supplied to
  `platform_add_app()`, which generates the version resource and
  PerMonitorV2/common-controls manifest. Do not restore a hand-written `.rc`
  or duplicate manifest.

## Build

- Built with CMake/Ninja through the vendored dd driver (`dd.ps1`,
  PowerShell 7.4+), which sets up MSVC.
- C++20, x64, `/W3`, `/EHsc`; keep the tree warning-clean.
- `.\dd.ps1 build` builds both configurations.
- `.\dd.ps1 test --label ergo-active` builds both configurations and runs four
  headless tests per configuration: smoke, rolling-window/persistence-format
  (32 assertions), dashboard layout/input/event/reminder/reactor policy
  (38 assertions), and the embedded manifest check. No test opens the
  dashboard or launches the visible app.
- Keep `CMAKE_BUILD_PARALLEL_LEVEL=2` for focused verification. Do not launch
  the dashboard or disturb a running user instance while building/testing.

## Conventions and preserved behavior

- Keep the dashboard charts, stat cards, slider, numeric editor, keyboard
  anchor/focus behavior, menus, tray policy and reminders; do not replace them
  with a generic list or placeholder.
- `dashboard_layout::measure()` is the single source of control geometry and
  runs from `pf::frame_reactor::handle_size()` and DPI-change events. Never
  move or create controls from paint.
- The app samples `GetLastInputInfo` every 10 seconds through the isolated
  Windows adapter. A locked session is away from the keyboard.
- Breaks and micro-pauses are detected by `usage_data::step()` and reported to
  `daily_stats` through `step_events`, never inferred from a notification.
- Usage data ages missed timer periods as idle ticks, including restart and
  sleep gaps. Breaks completed in a gap count only when the gap began today.
- Usage/history flushes every five minutes, on end-session, on exit and during
  teardown. The 20-20-20 reminder yields to an overdue break warning.
- Tray urgency remains green → yellow → red; tooltip updates, balloon tips,
  `NIF_SHOWTIP`, `NOTIFYICON_VERSION_4`, keyboard selection/context-menu anchor,
  and `TaskbarCreated` re-add recovery are required.
- `pf::try_lock_named_instance("Local\\ergo-active-single-instance")` preserves
  the legacy mutex identity. A second process sends the registered
  `ergo-active-show-window` broadcast signal and exits; it must not use
  `FindWindow` heuristics.
- The neutral data/presentation files must not include Windows headers. Generic
  GUI, tray, settings and file behavior belongs in libdesktop; only domain
  activity/session observation belongs in `win-services.h` behind a contract.
