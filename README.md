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

Prebuilt Windows installers are available on the [Releases page][releases]:

- **Latest build** — automatically rebuilt on every push to `main` (pre-release).
- **`v*` tags** — versioned releases.

Download `apm-tracker-<version>.msi` from a release and run it.

[releases]: https://github.com/TomasMaggi/actions-per-minute-tracker/releases

## Install

1. Download `apm-tracker-<version>.msi` from the [Releases page][releases].
2. Run it and accept the UAC prompt (it installs for all users).
3. The installer places `actions-per-minute-tracker.exe` in
   `C:\Program Files\APM Tracker\` and adds an **APM Tracker** shortcut to your
   desktop.
4. Tick **Launch APM Tracker** on the last page to start it right away.

Silent install (no UI):

```powershell
msiexec /i apm-tracker-1.0.0.msi /qn
```

Uninstall from **Settings → Apps → Installed apps → APM Tracker**, or:

```powershell
msiexec /x apm-tracker-1.0.0.msi /qn
```

Uninstalling removes the program files but keeps your settings and sessions
(see [Where data is stored](#where-data-is-stored)).

### Where data is stored

The tracker writes its settings, logs and session CSVs to
`%APPDATA%\APM Tracker\`:

- `settings.xml` — configuration (see [Configuration](#configuration)).
- `apm-tracker.log` — log file.
- `sessions\` — one CSV per finished session plus `index.csv`.

**Portable mode:** if you place a file named `portable` (or a `settings.xml`)
next to `actions-per-minute-tracker.exe`, the tracker stores its data in that
folder instead. This is how the standalone exe built with `build.ps1` behaves
when a `settings.xml` already sits next to it.

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
`apm-tracker-1.1.0.msi` attached and auto-generated release notes. The rolling
`main` build is published as a **pre-release**, so the newest versioned tag stays
as the "Latest" release.

To build a specific version locally:

```powershell
.\build.ps1 -Version 1.1.0
.\installer\build-installer.ps1 -Version 1.1.0
```

## Features

- Live APM overlay that floats on top of your game.
- APM-over-time graph window (place it on a second monitor).
- Keyboard and mouse actions tracked separately.
- Session stats: elapsed time, current APM, session average, 5-minute average,
  peak APM, key APM, click APM and total actions.
- **eAPM (approx.)** — an effective-APM estimate for AoE2 that ignores held-key
  auto-repeat. Enabled by the `aoe2` preset.
- **Live replay analysis** — while a game is being recorded the tracker parses
  the growing `.aoe2record` and shows the real APM / eAPM for your player,
  matching how AoE2Insights computes it (redundant repeated commands are
  dropped). It waits for a new recording instead of reading old ones, and saves
  the finished game when you stop the session.
- Session start/stop with a configurable global hotkey, so you only record while
  you play.
- Each session is saved to a CSV file you can review later.
- Ships as an MSI installer (and can still be built/run as a portable exe).

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

### Build the installer

The MSI is built with [WiX](https://wixtoolset.org/) (installed as a .NET tool,
no Visual Studio required):

```powershell
dotnet tool install --global wix --version 5.0.2
.\installer\build-installer.ps1
```

This produces `Release\win64\apm-tracker-<version>.msi`.

## Usage

1. Run `Release\win64\actions-per-minute-tracker.exe`, or after installing the
   MSI launch **APM Tracker** from the desktop shortcut.
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

The tracker is configured through a file named `settings.xml`. On a normal
(installed) setup it lives in `%APPDATA%\APM Tracker\settings.xml` and is
created automatically with defaults the first time you run the tracker. In
portable mode (a `portable` marker, or a `settings.xml` placed next to the
executable) it lives next to the executable instead. A commented template is
provided in [`settings.example.xml`](./settings.example.xml) — the tracker
never reads the example, so copy the values you want into `settings.xml`.

### How to change a setting

1. **Close the tracker first.** Settings are read once at startup, so changes
   take effect only after you restart it. (Closing the tracker also writes the
   current window positions back to this file.)
2. Open `settings.xml` in any text editor (Notepad works). It is in
   `%APPDATA%\APM Tracker\` for a normal install, or next to the executable in
   portable mode. If it does not exist yet, run the tracker once, then close it.
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
| `overlay_metric` | `apm`, `eapm`                | Which value the small overlay shows (`eapm` also enables eAPM).         |
| `overlay`        | `visible`, `x`, `y`          | Overlay visibility and position (`-1` = automatic, top-right).          |
| `graph`          | `x`, `y`, `width`, `height`  | Graph window position and size (`-1` = automatic, secondary monitor).   |
| `rec_analysis`   | `true`, `false`              | Read the in-progress `.aoe2record` for live APM/eAPM; finalize on session stop (default `true`). |
| `rec_folder`     | path                         | Replay folder override. Empty = auto-detect.                              |
| `eapm_dedup_ms`  | milliseconds                 | eAPM dedup window: identical command within this time counts once (default `2000`). |
| `eapm_consecutive` | `true`, `false`            | Drop only immediate repeats instead of any repeat within the window (default `false`). |
| `eapm_ignore_game` | `true`, `false`           | Exclude GAME settings toggles from eAPM (default `true`).                 |
| `live_eapm_debounce_ms` | milliseconds          | Dedup window for the live hook eAPM: repeated key/click within this time counts once (default `300`). |
| `replay_hotkey`  | e.g. `Ctrl+Shift+R`          | Manually re-analyze the newest replay.                                    |

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

Each finished session is written to the `sessions\` folder in the data
directory (`%APPDATA%\APM Tracker\sessions\`, or next to the executable in
portable mode):

- `sessions\YYYYMMDD-HHMMSS.csv` — a header with the summary plus one row per
  second (`second,raw,eapm,keyboard,mouse`).
- `sessions\index.csv` — one summary row per session.

## eAPM (approximation)

AoE2's "effective APM" is not officially documented. This tracker uses a
deliberately conservative estimate: it ignores **held-key auto-repeat** (only the
first keydown of a held key counts), drops repeated keys/clicks within
`live_eapm_debounce_ms`, and ignores modifier keys. It is labeled as approximate
and is only enabled by the `aoe2` preset (or `eapm`).

## Live replay analysis

For the real number, the tracker watches the replay folder
(`%USERPROFILE%\Games\Age of Empires 2 DE\<profile>\savegame\`) and starts
reading as soon as a recording is being written (the file is growing). It never
reads a pre-existing finished replay at startup, so it waits for the next game
to begin. It parses the recorded action stream for your player and computes:

- **APM** — actions per minute, the raw command count.
- **eAPM** — actions per minute with redundant commands dropped. A command is
  redundant when an identical one (same type, target and selected units)
  repeats within `eapm_dedup_ms`. GAME settings toggles are excluded when
  `eapm_ignore_game` is set. This mirrors how AoE2Insights treats spam clicks
  and repeated orders.

Results appear as a `LIVE APM ... eAPM ...` line plus an eAPM/APM timeline plot
in the graph window while the game runs. When you stop the session with the
hotkey the final recording is analyzed and saved to
`sessions\rec-<timestamp>.csv`, and the line switches to `Rec`. Pausing a game
stops the file from growing, which is not treated as the end of the match. Press
`replay_hotkey` (default `Ctrl+Shift+R`) to re-analyze the newest replay on
demand.

The replay parser is a minimal, self-contained port of the `aoc-mgz` DE body
format. It reads only the operation stream, so it is resilient to game-patch
changes in the header. If a recording cannot be parsed, the tracker falls back
to the live hook estimate.

## Project structure

```
main.cpp                     Windows, input hooks, graph/overlay rendering
counter.cpp / counter.h      Counter class: raw/eAPM/key/mouse stats, sessions
rec.cpp / rec.h              .aoe2record parser (DE operation stream)
eapm.cpp / eapm.h            APM/eAPM computation with redundant-command dedup
third_party/puff.*           Mark Adler's puff inflate (raw deflate header)
settings.cpp / settings.h    settings.xml load/save (XmlLite)
session.cpp / session.h      CSV session persistence
log.cpp / log.h              Timestamped file logger
tests/counter_tests.cpp      Unit tests for the counter and rec/eAPM logic
actions-per-minute-tracker.ico  Application icon (embedded into the exe)
app.rc                       Windows resource script: embeds the icon
build.ps1                    Build script (clang++)
VERSION                      Single source of truth for the version
settings.example.xml         Commented settings template
installer/                   WiX MSI installer (apm-tracker.wxs, license.rtf, build script)
.github/workflows/build.yml  CI: format check, tests, builds, releases
```

## Troubleshooting

### Windows SmartScreen warning

The executable is not code-signed, so Windows may show a SmartScreen warning
the first time you run it. Click **More info → Run anyway** to continue.

### Where are the logs?

The tracker writes `apm-tracker.log` in the data directory
(`%APPDATA%\APM Tracker\`, or next to the executable in portable mode),
including session start/stop events.

## License

Released under the [MIT License](./LICENSE).
