# ergo-active

[![Build](https://github.com/ZacWalk/ergo-active/actions/workflows/build.yml/badge.svg)](https://github.com/ZacWalk/ergo-active/actions/workflows/build.yml)

ergo-active is a small Windows x64 break reminder. It monitors keyboard and
mouse activity, keeps a rolling activity window, runs from the system tray, and
reminds you to take regular breaks.

The application is a libdesktop `ui::app`, and its dashboard is a
`ui::element_host`. Its element tree contains the drawn menu bar, custom
dashboard canvas, shared slider and numeric edit control. The canvas draws
stat cards, a four-hour activity graph and usage pie chart through
`render::draw_context`; platform services own the tray, settings, clipboard,
persistence and single-instance signaling.

## Features

- **Activity monitoring** — samples `GetLastInputInfo` every ten seconds
  without hooks or drivers. A locked session counts as time away.
- **Break reminders** — configurable 20–120 minute interval, green/yellow/red
  tray urgency, rotating posture tips and the 20-20-20 eye rule. Overdue break
  warnings always win over the eye reminder.
- **Dashboard** — dark stat cards, keyboard/mouse/break activity graph, daily
  usage pie chart, DPI-aware slider and numeric edit control with keyboard focus.
- **Daily history** — up to 30 days in
  `%APPDATA%\ergo-active\history.csv`; the rolling window survives restarts in
  `%APPDATA%\ergo-active\usage.dat`. Missed timer periods, application gaps,
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

The executable is `exe\ergo-active-64.exe` (Debug adds `d`). The application
links the four libdesktop libraries through `libdesktop::ui`; the sibling
`..\libdesktop` checkout is used for local development.

The labelled tests cover the smoke target, rolling window/history persistence
and failure reporting, headless dashboard layout/rendering/input/reminder
policy, shared tray and atomic-file publication failure, and the generated
PerMonitorV2 manifest. They do not open the visible dashboard or attach to a
running user instance.

## License

[CC0 1.0 Universal](LICENSE) — public domain. The executable statically links
the LGPL-2.1-or-later [libdesktop](https://github.com/ZacWalk/libdesktop)
platform layer; its source and pinned recipe remain available so users can
relink against a modified library.
