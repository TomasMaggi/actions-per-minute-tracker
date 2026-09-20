# Actions Per Minute Tracker

[![Build](https://github.com/TomasMaggi/actions-per-minute-tracker/actions/workflows/build.yml/badge.svg)](https://github.com/TomasMaggi/actions-per-minute-tracker/actions/workflows/build.yml)

An Actions Per Minute (APM) tracker that runs in the background and gives you
real-time stats on your keyboard and mouse activity.

It shows a small always-on-top **APM overlay** on top of your game, plus a
separate **graph window** that plots your APM over the whole session and shows
session stats.

This project currently only supports **Windows**.

## Download

Prebuilt Windows binaries are available on the [Releases page][releases]:

- **Latest build** — automatically rebuilt on every push to `main`.
- **`v*` tags** — versioned releases.

Download `actions-per-minute-tracker.exe` from the latest release and run it —
no installation required.

[releases]: https://github.com/TomasMaggi/actions-per-minute-tracker/releases

## Features

- Live APM overlay that floats on top of your game.
- APM-over-time graph window (place it on a second monitor).
- Session stats: elapsed time, current APM, session average, 5-minute average,
  peak APM and total actions.
- Session start/stop with a global hotkey, so you only record while you play.
- No installation required — it is a single `.exe`.

## Requirements

- Windows 10 or 11.
- [LLVM / clang++](https://releases.llvm.org/) on your `PATH` (or installed to
  the default `C:\Program Files\LLVM`). `clang++` automatically detects the
  Visual Studio C++ headers/libraries and the Windows SDK, so no extra setup is
  needed.

## Build

From the repository root in PowerShell:

```powershell
.\build.ps1
```

This compiles `main.cpp` and `counter.cpp` with `clang++` and produces:

```
Release\win64\actions-per-minute-tracker.exe
```

For a debug build:

```powershell
.\build.ps1 -Configuration Debug
```

You can also pass a custom output directory:

```powershell
.\build.ps1 -OutputDir .\out
```

If PowerShell blocks the script, run it with:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

## Usage

1. Run `Release\win64\actions-per-minute-tracker.exe`.
2. The small **APM overlay** appears in the upper-right corner of the primary
   screen, showing your current APM.
3. The **graph window** opens on the secondary monitor if you have one
   (otherwise on the primary monitor). It stays always on top. Drag it wherever
   you like.
4. The tracker starts **paused**. Press the session hotkey to begin recording.
5. To quit, **close the graph window**. There is no console window.

> The tracker runs as a background GUI application, so no console/terminal
> window is shown when it starts.

### Session control

Press **Shift + Backspace** to start a session, and press it again to stop.
Starting a new session clears the previous data. While stopped the graph and
stats freeze so you can review the finished game, and the overlay shows the
final APM.

The graph header shows a state indicator:

- `REC` (green) — a session is running and actions are being recorded.
- `PAUSED` (amber) — recording is stopped.

### Graph header

| Field     | Meaning                                            |
| --------- | -------------------------------------------------- |
| `Time`    | Elapsed session time.                              |
| `APM`     | Current APM (actions over the trailing 60s).       |
| `Avg`     | Session average APM.                               |
| `5m`      | Average APM over the last 5 minutes.               |
| `Peak`    | Highest APM reached during the session.            |
| `Actions` | Total keyboard + mouse actions this session.       |

### Controls

| Key                 | Action                        |
| ------------------- | ----------------------------- |
| `Shift` + `Backspace` | Start / stop the session.   |

> The tracker uses a global low-level keyboard/mouse hook, so it counts actions
> even when your game has focus. Because of that, you can leave it running in
> the background the whole time.

## Project structure

```
main.cpp                     Window creation, input hooks, graph rendering
counter.cpp                  Thread-safe action counting and session statistics
counter.h                    Public API for the counter
build.ps1                    Build script (clang++)
.github/workflows/build.yml  CI: builds and publishes releases on push
```

## Troubleshooting

### Windows SmartScreen warning

The executable is not code-signed, so Windows may show a SmartScreen warning
the first time you run it. Click **More info → Run anyway** to continue.

## License

Released under the [MIT License](./LICENSE).
