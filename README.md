# ergo-active

[![Build](https://github.com/ZacWalk/ergo-active/actions/workflows/build.yml/badge.svg)](https://github.com/ZacWalk/ergo-active/actions/workflows/build.yml)

ergo-active is a small Windows x64 break reminder. It monitors keyboard and
mouse activity, keeps a rolling activity window, runs from the system tray, and
reminds you to take regular breaks.

The dashboard is a `libdesktop` `pf::frame_reactor`: the shared core owns the
window, message loop, GDI drawing surface, DPI events, focus events, timers and
menus. Shared system services own the tray, registry settings and atomic file
publication. The app keeps its custom dashboard presentation (stat cards,
four-hour activity graph, usage pie chart, delay slider and numeric editor)
rather than reducing it to generic widgets.

## Features

- **Activity monitoring** — samples `GetLastInputInfo` every ten seconds
  without hooks or drivers. A locked session counts as time away.
- **Break reminders** — configurable 20–120 minute interval, green/yellow/red
  tray urgency, rotating posture tips and the 20-20-20 eye rule. Overdue break
  warnings always win over the eye reminder.
- **Dashboard** — dark stat cards, keyboard/mouse/break activity graph, daily
  usage pie chart, DPI-aware slider and numeric editor with keyboard focus.
- **Daily history** — up to 30 days in
  `%APPDATA%\ergo-active\history.csv`; the rolling window survives restarts in
  `%APPDATA%\ergo-active\usage.dat`. Missed timer periods, application gaps
  and sleep age as time away.
- **System tray** — dynamic urgency icons, live tooltip, balloon tips,
  keyboard-selection/context-menu anchor, `TaskbarCreated` recovery and
  single-instance show behavior.
- **Persistence diagnostics** — failed history/usage publication and tray
  publication are returned through testable seams and logged by the app instead
  of being silently discarded.

## Building

Requires Windows x64 and Visual Studio with the Desktop C++ workload. The
vendored [dd](https://github.com/ZacWalk/dd) runtime locates Visual Studio,
CMake and Ninja.

```powershell
$env:CMAKE_BUILD_PARALLEL_LEVEL = 2
.\dd.ps1 build                    # Debug and Release
.\dd.ps1 test --label ergo-active # headless tests only
```

The app records the published libdesktop revision
`31aae322af6e0696a9597726705b1121bd31580b` in
`cmake/dd-dependencies.json`. A sibling `..\libdesktop` checkout is used for
local development; a fresh checkout fetches the pin. The executable is
`exe\ergo-active-64.exe` (Debug adds `d`).

The four labelled tests per configuration cover the smoke target, rolling
window/history persistence and failure reporting, headless dashboard
layout/input/event/reminder policy including the shared edit reactor and
service teardown, shared tray publication failure, and the generated
PerMonitorV2 manifest. The model suite reports 32 assertions and the
dashboard/reactor suite reports 38 assertions per configuration. They do not
open the visible dashboard or attach to a running user instance.

## License

[CC0 1.0 Universal](LICENSE) — public domain. The executable statically links
the LGPL-2.1-or-later [libdesktop](https://github.com/ZacWalk/libdesktop)
platform layer; its source and the pinned recipe remain available so users can
relink against a modified library.
