# {canscope}

*[Русская версия](README_ru.md)*

CAN bus sniffer and SAE J1939 protocol analyzer. Reads CAN frames (`candump` format or native SocketCAN), decodes them via a J1939 Digital Annex (xlsx/csv), and shows the result in an interactive TUI or as JSON.

![demo](canscope-demo.gif)

## Features

- **TUI** — interactive terminal UI; per-CAN-ID modes: `brief`, `verbose`, `charts`, `manual`
- **Live charts** — per-SPN scatter plot with auto-scaled Y axis
- **Manual / reverse-engineering** — build custom SPNs over a live CAN ID by picking byte/bit ranges
- **Regex filter** — over CAN ID / PGN label
- **Headless** — stream decoded values as NDJSON to stdout
- **Discover** — one pass over the bus, dump a JSON tree of all PGNs/SPNs
- **Record** — capture every frame to SQLite (delta-encoded, rotating)
- **Playback** — replay received frames or your own config

## Build

Requires clang++ (C++20), CMake ≥ 3.13, Ninja, and system libs: boost, sqlite3, zlib, icu. Other deps are fetched automatically.

```bash
make build          # native (dynamic). Also: build_static, build_arm64, install
make list           # all targets
```

Binary: `build/native/canscope`. See `make list` for static / arm64 / Docker targets.

## Usage

Modes are mutually exclusive; default is TUI.

```bash
# TUI
canscope -e "candump can0" -j1939-csv thirdparty/j1939da_2018.csv

# Native SocketCAN (Linux), or read from stdin
canscope -can can0 -j1939-csv thirdparty/j1939da_2018.csv
candump can0 | canscope -j1939-csv thirdparty/j1939da_2018.csv
```

| Mode | Flag | Description |
|------|------|-------------|
| TUI | *(default)* | Interactive full-screen UI |
| Discover | `-discover` | Dump PGN/SPN structure to stdout or `-of <file>` |
| Headless | `-hl` | Stream decoded values as NDJSON |
| Record | `-rec -db <file>` | Write decoded values + timestamps to SQLite |

Source: `-e <cmd>` (e.g. `"candump can0"`), `-can <list>` (native, e.g. `can0,can1` or `any`), or stdin. J1939 DA: `-j1939-csv` (faster) or `-j1939-xlsx`. Run `canscope -h` for all flags.

> Tested with the J1939 Digital Annex 2018 edition.

## Roadmap

NMEA 2000 and CANopen decoding, plus assorted UI/perf improvements.
