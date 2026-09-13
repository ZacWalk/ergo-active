# ergo-active

[![Build](https://github.com/ZacWalk/ergo-active/actions/workflows/build.yml/badge.svg)](https://github.com/ZacWalk/ergo-active/actions/workflows/build.yml)

A native Win32 C++ desktop application that monitors keyboard and mouse activity
and reminds you to take regular breaks. It runs in the system tray, tracks input
over a rolling window, detects idle breaks, and notifies you when you have been
working too long without a rest.

Only uses about 2mb of ram and almost no CPU.

![ergo-active dashboard](screen-shot.png)

## Features

- **Activity monitoring** — tracks keyboard and mouse activity in real time via
  `GetLastInputInfo`, with no hooks or drivers. Counts ~20-second micro-pauses as
  well as full 3-minute breaks, and treats a locked session as time away.
- **Break reminders** — a configurable 20–120 minute interval, with the tray icon
  shifting green → yellow → red as you approach and exceed it. Balloon
  notifications carry rotating posture tips. The 20-20-20 eye-strain rule yields
  to an overdue break warning so the two never collide.
- **Dashboard** — a dark-themed window with stat cards for active time, next
  break countdown, breaks taken, longest stretch and a daily ergonomic score.
  A four-hour activity graph splits keyboard from mouse; a pie chart breaks the
  day into keyboard, mouse, machine-on, locked and off time. Per-monitor DPI aware.
- **Daily history** — up to 30 days of per-day statistics in
  `%APPDATA%\ergo-active\history.csv`, flushed every five minutes and at
  shutdown. The rolling window survives restarts. The score is measured against
  *your* configured interval, rewarding regular breaks and penalising long
  unbroken stretches.
- **System tray** — dynamic colour-coded icon, live tooltip, single-instance
  behaviour, and automatic recovery if Explorer restarts.

## Building

Requires Windows x64 and Visual Studio with the Desktop C++ workload. The tree is
warning-clean at `/W4`. The vendored [dd](https://github.com/ZacWalk/dd) runtime
locates Visual Studio and uses the CMake and Ninja that ship with it.

```powershell
.\dd.ps1 build        # both configurations
.\dd.ps1 test         # build and run the suite
.\dd.ps1 run          # build, then launch
```

Binaries are written to `exe\ergo-active-64.exe`, with a `d` suffix for Debug.

## Documentation

[AGENTS.md](AGENTS.md) — conventions for contributors and coding agents.

## License

[CC0 1.0 Universal](LICENSE) — public domain.
