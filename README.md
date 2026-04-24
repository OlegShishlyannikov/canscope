# {canscope}

*[Русская версия](README_ru.md)*

CAN bus sniffer and SAE J1939 protocol analyzer. Reads CAN frames in `candump` format, decodes them using a J1939 Digital Annex (xlsx or csv), and presents results in an interactive terminal UI or as JSON output.

![demo](canscope-demo.gif)

## Features

- **TUI mode** - full-screen interactive terminal interface (FTXUI). Four display modes per CAN ID: `brief`, `verbose`, `charts`, `manual`
- **Per-SPN live charts** - scatter plot (braille canvas) per numeric SPN with auto-scaled Y axis, switchable between all SPNs of the PGN.
- **Regex search/filter** - filter the CANID list by regex over identifier and PGN label. Also works in "can player" and "parameters export" dialog.
- **Headless mode** - newline-delimeted JSON output to stdout, for scripting and automation
- **Discover mode** — probe a live bus for one pass, emit a JSON tree of every PGN seen with its full SPN breakdown (positions, resolutions, units, ranges). Useful for reverse-engineering or building configuration
  files without running the TUI. Writes to stdout (newline-delimeted JSON) or `-of <file>` (array JSON).
- **Recording** - Per-frame capture to SQLite via a dedicated non-blocking pipeline (lock-free queue + background decoder thread). Every frame is archived — no 30 Hz sampling, no aggregation. Decoded SPN values are stored as gzip-compressed JSON, each with its own timestamp. Supports kernel timestamps from `candump -t a` / `-t A`
- **CAN playback** - replay received CAN frames or play your own configuration
- **Manual mode / reverse-engineering** — build custom SPN definitions directly on top of a live CAN ID without a J1939 DA entry. Pick any byte/bit range in the payload, set resolution/offset/unit/endianness, and see
  the decoded value update live next to the raw bytes (with the selected bits highlighted in red). Supports up to 5 non-contiguous fragments per SPN, little/big endian, and up to 8 custom SPNs per CAN ID. Custom SPNs
  are first-class citizens: they appear in the `charts` tab, in newline-delimeted JSON in headless mode, and in the SPN export dialog
- **Real-time** - 30 fps UI refresh

### Screenshots

**SPN viewer** - `verbose` tab, full PGN/SPN breakdown with live values
![spn_viewer](pics/spn_viewer.png)

**Live charts** - per-SPN scatter plot with auto-scaled Y axis
![charts](pics/charts.png)

**Reverse engineering** - `manual` tab, build custom SPN from raw bits with live payload highlighting
![reverse-engineering](pics/reverse-engineering.png)

**Regex search** - filter by CAN ID / PGN label
![regex_search](pics/regex_search.png)

**Parameter export** - select SPNs across CAN IDs for JSON export
![spn_export_view](pics/spn_export_view.png)

**Playback** - replay received CAN bus frames or replay your own configuration (based on J1939 DA or custom SPNs)
![playback](pics/playback.png)

## Build

**Requirements:**
- clang++ with C++20 support
- CMake >= 3.13
- Ninja
- System libraries: boost (signals2, spirit, phoenix, regex), sqlite3, zlib, icu

Dependencies fetched automatically via CMake FetchContent:

- [FTXUI](https://github.com/ArthurSonzogni/FTXUI) - terminal UI framework
- [tiny-process-library](https://gitlab.com/eidheim/tiny-process-library) - subprocess management
- [sqlite_modern_cpp](https://github.com/SqliteModernCpp/sqlite_modern_cpp) - modern C++ SQLite wrapper
- [xlnt](https://github.com/xlnt-community/xlnt) - xlsx reading
- [lely-core](https://gitlab.com/lely_industries/lely-core) - CANopen protocol stack
- [fmt](https://github.com/fmtlib/fmt) - text formatting
- [nlohmann/json](https://github.com/nlohmann/json) - JSON library
- [clipp](https://github.com/muellan/clipp) - CLI argument parsing

### Available targets

```bash
make list              # Show all targets

make build             # Native build (dynamic linking)
make build_static      # Native build (static linking)
make install           # Install to PREFIX (default /usr/local), requires patchelf
make install_static    # Install static binary to PREFIX

make docker-run ARGS='...'       # Build and run in Docker (cross-platform)
make build_arm64                 # Cross-compile for arm64 (dynamic)
make build_arm64_static          # Cross-compile for arm64 (static)

make clean             # Remove all build artifacts
```

### Native build

```bash
make build
./build/native/canscope -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx
# or with CSV (faster parsing)
./build/native/canscope -e "candump can0" -j1939-csv thirdparty/j1939da_2018.csv
```

### Docker (cross-platform)

Works on Linux, macOS (?), and Windows (?). Requires only Docker and Make.

```bash
# TUI mode - local CAN interface
make docker-run ARGS='-e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx'

# TUI mode - remote CAN interface via SSH (no data if will ask password - use public key access or sshpass utility)
make docker-run ARGS='-e "ssh user@remote candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx'

# Discover mode - find out what PGNs/SPNs are on the bus
make docker-run ARGS='-discover -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx'

# Headless mode - stream decoded values
make docker-run ARGS='-hl -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx'
```

### Cross-compile for arm64

```bash
make build_arm64           # dynamic linking
make build_arm64_static    # static linking
```

Requires Docker. SSH keys from `~/.ssh` and `/etc/hosts` are forwarded into the build container for fetching private git dependencies.

## Usage

All operating modes are mutually exclusive. If none is specified, TUI mode is used.

```bash
# TUI mode (default) - interactive terminal interface
canscope -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx

# Discover mode - output PGN/SPN structure (no values) to stdout or file
canscope -discover -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx
canscope -discover -of discovered.json -e "candump can0" -j1939-csv thirdparty/j1939da_2018.csv

# Headless mode - stream all decoded values (NDJSON) to stdout
canscope -hl -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx

# Record mode - capture every frame to SQLite (per-frame, non-blocking)
canscope -rec -db recording.db -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx

# Record with kernel timestamps (absolute unix seconds via `-t a`, or wall-clock via `-t A`)
canscope -rec -db recording.db -e "candump -t a can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx
canscope -rec -db recording.db -e "candump -t A can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx

# Read from stdin (pipe)
candump can0 | canscope -j1939-csv thirdparty/j1939da_2018.csv
candump -t a can0 | canscope -rec -db recording.db -j1939-csv thirdparty/j1939da_2018.csv
```

> **Note:** J1939 decoding has only been tested with the Digital Annex 2018 edition. Other editions may work but are not guaranteed.

### Modes

| Mode | Flag | Description |
|------|------|-------------|
| TUI | *(default)* | Interactive full-screen terminal UI |
| Discover | `-discover` | Output PGN/SPN structure (no values) to stdout or file (`-of`) |
| Headless | `-hl` | Stream all decoded PGN/SPN values as NDJSON to stdout |
| Record | `-rec` | Write all decoded PGN/SPN values + timestamps to SQLite (`-db`) |

### CLI flags

| Flag | Long form | Description |
|------|-----------|-------------|
| `-j1939-xlsx` | | J1939 Digital Annex xlsx file |
| `-j1939-csv` | | J1939 Digital Annex csv file (faster parsing) |
| `-e` | `--execute-command` | Command to read CAN frames from (e.g. `"candump can0"`) |
| `-discover` | | Discover mode |
| `-hl` | `--headless` | Headless mode |
| `-rec` | `--record` | Record mode |
| `-of` | `--output-file` | Output file path (used with `-discover`) |
| `-db` | `--database` | SQLite database path (required with `-rec`) |
| `-h` | `--help` | Show help |

### Recording format

`-rec` writes a single SQLite database with one row per CAN frame. The schema is optimized for disk usage — raw payloads are not stored (only decoded SPN values in compressed form):

| Column  | Type    | Notes |
|---------|---------|-------|
| `id`    | INTEGER | PK, autoincrement |
| `ts_ms` | INTEGER | Unix milliseconds. Sourced from `candump -t a`/`-t A` when present, otherwise captured at parse time |
| `iface` | TEXT    | e.g. `can0` |
| `canid` | TEXT    | 3-digit (SFF) or 8-digit (EFF) hex |
| `pgn`   | INTEGER | J1939 PGN, `NULL` if the frame isn't J1939 or the Digital Annex wasn't provided |
| `spns`  | BLOB    | gzip of a JSON array. Each element is `{ts_ms, spn, name, value, unit}` |

Indexes: `ts_ms`, `(canid, ts_ms)`, `(pgn, ts_ms) WHERE pgn IS NOT NULL`. Opened with `journal_mode=WAL`, `synchronous=NORMAL`. Inserts are batched into 1-second transactions.

Quick query example (requires SQLite with JSON1 — decode the gzip blob externally first, or use a helper script):

```sql
SELECT ts_ms, canid, pgn FROM frames WHERE ts_ms BETWEEN ? AND ? ORDER BY ts_ms;
SELECT COUNT(*) FROM frames WHERE pgn = 61444;
```

## Roadmap

- **NMEA 2000 protocol support** - NMEA 2000 decoding using [canboat](https://github.com/canboat/canboat) PGN database (JSON). Same 29-bit CAN ID as J1939, requires Fast Packet protocol implementation
- **CANopen protocol support** - CANopen decoding alongside J1939 (11-bit CAN ID, SDO/PDO/NMT)
- **Other small features and enhancements** - UI improvements, performance optimizations, additional export formats
