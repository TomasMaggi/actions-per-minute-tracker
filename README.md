# Actions Per Minute Tracker

[![Build](https://github.com/TomasMaggi/actions-per-minute-tracker/actions/workflows/build.yml/badge.svg)](https://github.com/TomasMaggi/actions-per-minute-tracker/actions/workflows/build.yml)

An Actions Per Minute (APM) tracker that runs in the background and gives you
real-time stats on your keyboard and mouse activity.

It shows a small always-on-top **APM overlay** on top of your game, plus a
separate **graph window** that plots your APM over the whole session and shows
session stats. It is built with Age of Empires 2 in mind, but works for any game
or for generic desktop use.

This project currently only supports **Windows**.

## Download

Prebuilt Windows binaries are available on the [Releases page][releases]:

- **Latest build** — automatically rebuilt on every push to `main`.
- **`v*` tags** — versioned releases.

Download `actions-per-minute-tracker.exe` from the latest release and run it —
no installation required.

[releases]: https://github.com/TomasMaggi/actions-per-minute-tracker/releases

## Versioning

The version lives in the [`VERSION`](./VERSION) file at the root of the
repository and is the single source of truth. It is embedded into the executable
at build time, so the running version appears in the graph window title (e.g.
`APM over time - v1.0.0`) and in `apm-tracker.log`.

The CI workflow derives the release version automatically:

- **Tag builds** (`v*`) — the version is the tag without the leading `v`
  (e.g. tag `v1.2.0` → version `1.2.0`). This creates a normal versioned release.
- **`main` builds** — the version is the `VERSION` value plus the short commit
  hash (e.g. `1.2.0+5a63e05`). This updates the rolling **Latest build** release.

### Cutting a release

1. Bump the version in `VERSION` (for example `1.0.0` → `1.1.0`).
2. Commit and push to `main`.
3. Create and push a matching tag:

   ```powershell
   git tag v1.1.0
   git push origin v1.1.0
   ```

CI then builds, tests, and publishes a release named `v1.1.0` with
`actions-per-minute-tracker.exe` attached and auto-generated release notes.

To build a specific version locally:

```powershell
.\build.ps1 -Version 1.1.0
```

## Features

- Live APM overlay that floats on top of your game.
- APM-over-time graph window (place it on a second monitor).
- Keyboard and mouse actions tracked separately.
- Session stats: elapsed time, current APM, session average, 5-minute average,
  peak APM, key APM, click APM and total actions.
- **eAPM (approx.)** — an effective-APM estimate for AoE2 that ignores held-key
  auto-repeat. Enabled by the `aoe2` preset.
- Session start/stop with a configurable global hotkey, so you only record while
  you play.
- Each session is saved to a CSV file you can review later.
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

This compiles the sources with `clang++` and produces:

```
Release\win64\actions-per-minute-tracker.exe
```

To build and run the unit tests as well:

```powershell
.\build.ps1 -Test
```

Other options:

```powershell
.\build.ps1 -Configuration Debug      # debug build
.\build.ps1 -OutputDir .\out          # custom output directory
powershell -ExecutionPolicy Bypass -File .\build.ps1   # if script execution is blocked
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

The default hotkey is **Shift + Backspace**: press it to start a session, and
press it again to stop. Starting a new session clears the previous data. While
stopped the graph and stats freeze so you can review the finished game, and the
overlay shows the final APM.

The graph header shows a state indicator:

- `REC` (green) — a session is running and actions are being recorded.
- `PAUSED` (amber) — recording is stopped.

### Graph header

| Field    | Meaning                                          |
| -------- | ------------------------------------------------ |
| `Time`   | Elapsed session time.                            |
| `Actions`| Total keyboard + mouse actions this session.     |
| `APM`    | Current APM (actions over the trailing 60s).     |
| `eAPM`   | Effective APM estimate (shown when enabled).     |
| `Avg`    | Session average APM.                             |
| `5m`     | Average APM over the last 5 minutes.             |
| `Peak`   | Highest APM reached during the session.          |
| `Keys`   | Current keyboard APM.                            |
| `Clicks` | Current mouse APM.                               |

When eAPM is enabled the graph draws two lines: green is raw APM and blue is
eAPM.

## Configuration

The tracker is configured through a file named `settings.xml` that lives **next
to the executable**. It is created automatically with defaults the first time
you run the tracker. A commented template is provided in
[`settings.example.xml`](./settings.example.xml).

### How to change a setting

1. **Close the tracker first.** Settings are read once at startup, so changes
   take effect only after you restart it. (Closing the tracker also writes the
   current window positions back to this file.)
2. Open `settings.xml` in any text editor (Notepad works). If it does not exist
   yet, run the tracker once, then close it.
3. Edit the values you want (see the table below).
4. Save the file and start the tracker again.

> `settings.xml` is plain XML. Element names and the `hotkey` value are
> case-insensitive. If the file is missing or a value is invalid, the tracker
> falls back to the defaults.

### Example contents

The tracker's built-in defaults are `preset=generic` with `eapm=false`. The
following example shows an AoE2 setup:

```xml
<?xml version="1.0"?>
<settings>
  <preset>aoe2</preset>
  <eapm>true</eapm>
  <hotkey>Shift+Backspace</hotkey>
  <overlay_metric>apm</overlay_metric>
  <overlay visible="true" x="-1" y="-1"/>
  <graph x="-1" y="-1" width="760" height="290"/>
</settings>
```

### Settings reference

| Setting          | Values                       | Meaning                                                                 |
| ---------------- | ---------------------------- | ----------------------------------------------------------------------- |
| `preset`         | `generic`, `aoe2`            | The `aoe2` preset enables eAPM.                                         |
| `eapm`           | `true`, `false`              | Enables the effective-APM approximation (ignores held-key auto-repeat). |
| `hotkey`         | e.g. `Shift+Backspace`       | Session start/stop hotkey (see below).                                  |
| `overlay_metric` | `apm`, `eapm`                | Which value the small overlay shows.                                    |
| `overlay`        | `visible`, `x`, `y`          | Overlay visibility and position (`-1` = automatic, top-right).          |
| `graph`          | `x`, `y`, `width`, `height`  | Graph window position and size (`-1` = automatic, secondary monitor).   |

Window positions are saved automatically when you close the tracker, so you can
just drag the windows where you want them instead of editing `x`/`y` by hand.

### Hotkey syntax

The `hotkey` value is a `+`-separated list of modifiers followed by a key, for
example `Shift+Backspace`, `Ctrl+Alt+F9` or `Ctrl+Shift+K`.

- **Modifiers:** `Ctrl` (or `Control`), `Alt`, `Shift`, `Win`.
- **Keys:** `A`–`Z`, `0`–`9`, `F1`–`F24`, `Backspace`, `Tab`, `Enter`, `Esc`,
  `Space`, `Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown`, the arrow
  keys (`Up`, `Down`, `Left`, `Right`), and `Numpad0`–`Numpad9`.

At least one modifier is recommended so the hotkey does not clash with normal
typing or in-game controls.

### Common examples

Enable the AoE2 preset with eAPM and make the overlay show eAPM:

```xml
<preset>aoe2</preset>
<eapm>true</eapm>
<overlay_metric>eapm</overlay_metric>
```

Use a different hotkey (for example `Ctrl+Alt+S`):

```xml
<hotkey>Ctrl+Alt+S</hotkey>
```

Hide the overlay but keep the graph window:

```xml
<overlay visible="false" x="-1" y="-1"/>
```

Use generic tracking (no eAPM) with the overlay showing raw APM:

```xml
<preset>generic</preset>
<eapm>false</eapm>
<overlay_metric>apm</overlay_metric>
```

Reset both windows to their automatic positions:

```xml
<overlay visible="true" x="-1" y="-1"/>
<graph x="-1" y="-1" width="760" height="290"/>
```

## Sessions

Each finished session is written to `sessions\` next to the executable:

- `sessions\YYYYMMDD-HHMMSS.csv` — a header with the summary plus one row per
  second (`second,raw,eapm,keyboard,mouse`).
- `sessions\index.csv` — one summary row per session.

## eAPM (approximation)

AoE2's "effective APM" is not officially documented. This tracker uses a
deliberately conservative estimate: it ignores **held-key auto-repeat** (only the
first keydown of a held key counts). Mouse clicks are always counted. It is
labeled as approximate and is only enabled by the `aoe2` preset (or `eapm`).

## Project structure

```
main.cpp                     Windows, input hooks, graph/overlay rendering
counter.cpp / counter.h      Counter class: raw/eAPM/key/mouse stats, sessions
settings.cpp / settings.h    settings.xml load/save (XmlLite)
session.cpp / session.h      CSV session persistence
log.cpp / log.h              Timestamped file logger
tests/counter_tests.cpp      Unit tests for the counter
build.ps1                    Build script (clang++)
VERSION                      Single source of truth for the version
settings.example.xml         Commented settings template
.github/workflows/build.yml  CI: format check, tests, builds, releases
```

## Troubleshooting

### Windows SmartScreen warning

The executable is not code-signed, so Windows may show a SmartScreen warning
the first time you run it. Click **More info → Run anyway** to continue.

### Where are the logs?

The tracker writes `apm-tracker.log` next to the executable, including session
start/stop events.

## License

Released under the [MIT License](./LICENSE).
