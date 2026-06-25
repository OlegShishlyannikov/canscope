# {canscope}

*[English version](README.md)*

Снифер CAN-шины и анализатор протокола SAE J1939. Читает CAN-фреймы (формат `candump` или нативный SocketCAN), декодирует по J1939 Digital Annex (xlsx/csv) и показывает результат в интерактивном TUI или как JSON.

![demo](canscope-demo.gif)

## Возможности

- **TUI** — интерактивный терминальный интерфейс; режимы на CAN ID: `brief`, `verbose`, `charts`, `manual`
- **Живые графики** — scatter-график по SPN с автошкалой по Y
- **Manual / reverse-engineering** — свои SPN поверх живого CAN ID по выбранным байтам/битам
- **Regex-фильтр** — по CAN ID / PGN-метке
- **Headless** — стрим декодированных значений в stdout как NDJSON
- **Discover** — один проход по шине, JSON-дерево всех PGN/SPN
- **Запись** — каждый фрейм в SQLite (delta-кодирование, ротация)
- **Playback** — воспроизведение принятых фреймов или своей конфигурации

## Сборка

Нужны clang++ (C++20), CMake ≥ 3.13, Ninja и системные либы: boost, sqlite3, zlib, icu. Остальные зависимости подтягиваются автоматически.

```bash
make build          # нативная (динамическая). Также: build_static, build_arm64, install
make list           # все таргеты
```

Бинарник: `build/native/canscope`. Static / arm64 / Docker — см. `make list`.

## Использование

Режимы взаимоисключающи; по умолчанию — TUI.

```bash
# TUI
canscope -e "candump can0" -j1939-csv thirdparty/j1939da_2018.csv

# Нативный SocketCAN (Linux) или чтение из stdin
canscope -can can0 -j1939-csv thirdparty/j1939da_2018.csv
candump can0 | canscope -j1939-csv thirdparty/j1939da_2018.csv
```

| Режим | Флаг | Описание |
|-------|------|----------|
| TUI | *(по умолчанию)* | Интерактивный полноэкранный UI |
| Discover | `-discover` | Структура PGN/SPN в stdout или `-of <file>` |
| Headless | `-hl` | Стрим декодированных значений как NDJSON |
| Record | `-rec -db <file>` | Запись значений + таймстемпов в SQLite |

Источник: `-e <cmd>` (напр. `"candump can0"`), `-can <list>` (нативно, напр. `can0,can1` или `any`) или stdin. J1939 DA: `-j1939-csv` (быстрее) или `-j1939-xlsx`. Все флаги — `canscope -h`.

> Тестировалось с J1939 Digital Annex редакции 2018.

## Roadmap

Поддержка NMEA 2000 и CANopen, плюс улучшения UI/производительности.
