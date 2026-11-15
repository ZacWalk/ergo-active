# ergo-active – agent instructions

ergo-active is a Windows break reminder built on libdesktop's `core`,
`platform`, `render` and `ui` libraries. [README.md](README.md) describes what
it does and how to build it.

## Build and test

- Build and test with `CMAKE_BUILD_PARALLEL_LEVEL=2`, through
  `.\dd.ps1 test --label ergo-active`. Never run an unfiltered `.\dd.ps1 test`
  or launch the app: both start the dashboard, and a second instance signals
  the user's running one to show itself.
- `dd.ps1` and `.dd/` are vendored [dd](https://github.com/ZacWalk/dd), with
  their hashes in [docs/dd-upstream.json](docs/dd-upstream.json); do not edit
  them. Project settings belong in `dd.psd1`.
- libdesktop is pinned twice, by the `GIT_TAG` in `CMakeLists.txt` and in
  `cmake/dd-dependencies.json`; keep the two equal. A sibling `..\libdesktop`
  checkout is used instead when present.
- Every target builds at `/W4` with no warnings, and `.clang-tidy` names the
  bug-finding and performance checks the code is kept clean against; a
  deliberate exception is a `// NOLINT(check): reason` on its line.
- `.clang-format` is the layout (tabs, Allman braces, 120 columns).
  `.\dd.ps1 fmt` applies it to `include`, `src` and `tests` only, given
  clang-format on PATH (Visual Studio installs one in `VC\Tools\Llvm\x64\bin`).

## Layers

Each source file in `src/` belongs to one layer:

- `data.h`, `dashboard-layout.h`, `dashboard-policy.h`, `app-services.h` – the
  platform-neutral activity model and file formats, control layout, reminder
  policy, and the service contracts the dashboard talks to.
- `ui-frame.h`, `main.cpp` – the dashboard's menus, content and logic, and the
  application lifecycle on libdesktop's `ui::shell`.
- `desktop-services.h` – adapters from those contracts to libdesktop's
  app-data, file, settings, clipboard and tray services.
- `win-services.h` – the only Windows code: `GetLastInputInfo` and session
  state. No other file includes a Windows header.

Generic GUI, tray, settings and file behavior belongs in libdesktop, not here.

## Rules

- Keep the dashboard's charts, stat cards, slider, numeric editor, keyboard
  focus, menus, tray policy and reminders; never replace them with a generic
  list or a placeholder.
- `dashboard_layout::measure()`, called from `main_frame::layout_content`, is
  the only source of control geometry.
- `usage_data::step()` detects breaks and micro-pauses and reports them to
  `daily_stats` through `step_events`; never infer one from a notification.
- `usage_data::age()` counts missed timer periods, restarts and sleep included,
  as idle. A break completed in a gap counts only when the gap began today.
- Usage and history flush every five minutes, at session end, on exit and in
  teardown.
- The instance lock is `Local\ergo-active-single-instance`. A second process
  sends the `ergo-active-show-window` signal and exits; never find the running
  instance by its window.
