# {canscope}

CAN bus sniffer and SAE J1939 protocol analyzer. Reads CAN frames in `candump` format, decodes them using a J1939 Digital Annex (xlsx or csv), and presents results in an interactive terminal UI or as JSON output.

![demo](canscope-demo.gif)

## Features

- **TUI mode** - full-screen interactive terminal interface (FTXUI). Multiple display modes per CAN ID: deployed, brief, verbose, manual, little-endian
- **Headless mode** - JSON output to stdout or file, for scripting and automation
- **Recording** - decoded J1939 SPN values saved to SQLite database with gzip compression and batch flushing
- **J1939 decoding** - PGN/SPN lookup, bit-level value extraction from payload. Supports xlsx and csv input formats
- **CAN playback** - replay recorded CAN frames
- **Custom SPN configuration** - per-parameter settings, parameter export
- **Real-time** - 30 fps UI refresh

## Build

**Requirements:**
- clang++ with C++23 support
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

# Headless mode - create report about collected PGNs and SPNs
make docker-run ARGS='-hl -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx -of output.json'
```

### Cross-compile for arm64

```bash
make build_arm64           # dynamic linking
make build_arm64_static    # static linking
```

Requires Docker. SSH keys from `~/.ssh` and `/etc/hosts` are forwarded into the build container for fetching private git dependencies.

## Usage

```bash
# TUI mode (default)
canscope -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx

# Headless - JSON to stdout
canscope -hl -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx

# Headless - JSON to file
canscope -hl -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx -of output.json

# Read from stdin (pipe)
candump can0 | canscope -j1939-xlsx thirdparty/j1939da_2018.xlsx

# Record to SQLite database
canscope -rec -db recording.db -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx

# Record + TUI
canscope -rec -db recording.db -tui -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx
```

> **Note:** J1939 decoding has only been tested with the Digital Annex 2018 edition. Other editions may work but are not guaranteed.

### CLI flags

| Flag | Long form | Description |
|------|-----------|-------------|
| `-j1939-xlsx` | | J1939 Digital Annex xlsx file |
| `-j1939-csv` | | J1939 Digital Annex csv file (faster parsing) |
| `-e` | `--execute-command` | Command to read CAN frames from (e.g. `"candump can0"`) |
| `-hl` | `--headless` | Headless mode (no TUI) |
| `-of` | `--output-file` | Output file path (headless mode) |
| `-rec` | `--record` | Record decoded values to SQLite |
| `-db` | `--database` | SQLite database path (required with `-rec`) |
| `-tui` | | Show TUI alongside recording |
| `-h` | `--help` | Show help |

## Roadmap

- **NMEA 2000 protocol support** - NMEA 2000 decoding using [canboat](https://github.com/canboat/canboat) PGN database (JSON). Same 29-bit CAN ID as J1939, requires Fast Packet protocol implementation
- **CANopen protocol support** - CANopen decoding alongside J1939 (11-bit CAN ID, SDO/PDO/NMT)
- **Other small features and enhancements** - UI improvements, performance optimizations, additional export formats
