# {canscope}

*[English version](README.md)*

Снифер CAN-шины и анализатор протокола SAE J1939. Читает CAN-фреймы в формате `candump`, декодирует их по J1939 Digital Annex (xlsx или csv) и показывает результат в интерактивном терминальном интерфейсе или как JSON.

![demo](canscope-demo.gif)

## Возможности

- **TUI-режим** — полноэкранный интерактивный терминальный интерфейс (FTXUI). Четыре режима отображения на CAN ID: `brief`, `verbose`, `charts`, `manual`
- **Живые графики по SPN** — scatter-график (braille canvas) на каждый числовой SPN с автошкалой по Y, переключаемый между всеми SPN одного PGN.
- **Regex-поиск/фильтр** — фильтрация списка CAN ID'ов регуляркой по идентификатору и PGN-метке. Работает также в диалогах "can player" и "parameters export".
- **Headless-режим** — вывод NDJSON (newline-delimited JSON) в stdout для скриптов и автоматизации
- **Discover-режим** — однопроходный опрос живой шины, выдаёт JSON-дерево всех увиденных PGN с полной разбивкой по SPN (позиции, разрешения, единицы, диапазоны). Полезно для reverse-engineering или генерации
  конфигов без запуска TUI. Пишет в stdout (NDJSON) или в `-of <file>` (JSON-массив).
- **Запись (Recording)** — пофреймовая запись в SQLite через отдельный non-blocking пайплайн (lock-free очередь + фоновый decoder-поток). Архивируется **каждый фрейм**: никакого 30 Hz сэмплирования, никакой агрегации. Декодированные SPN лежат как gzip-сжатый JSON, у каждого свой timestamp. Поддерживаются kernel-таймстемпы из `candump -t a` / `-t A`
- **CAN playback** — воспроизведение ранее принятых фреймов или своей конфигурации
- **Manual-режим / reverse-engineering** — построение собственных SPN поверх живого CAN ID без записи в J1939 DA. Выбираешь любой диапазон байт/бит в payload'е, задаёшь resolution/offset/unit/endianness и смотришь,
  как декодированное значение обновляется рядом с сырыми байтами (выбранные биты подсвечены красным). Поддерживается до 5 несмежных фрагментов на SPN, little/big endian и до 8 пользовательских SPN на CAN ID.
  Custom SPN — первоклассные: они видны во вкладке `charts`, в NDJSON в headless-режиме и в диалоге экспорта параметров
- **Real-time** — обновление UI на 30 fps

### Скриншоты

**SPN viewer** — вкладка `verbose`, полная разбивка PGN/SPN с живыми значениями
![spn_viewer](pics/spn_viewer.png)

**Live charts** — scatter-график по SPN с автошкалой по Y
![charts](pics/charts.png)

**Reverse engineering** — вкладка `manual`, построение своего SPN из сырых бит с подсветкой payload'а
![reverse-engineering](pics/reverse-engineering.png)

**Regex search** — фильтр по CAN ID / PGN-метке
![regex_search](pics/regex_search.png)

**Parameter export** — выбор SPN по нескольким CAN ID для экспорта в JSON
![spn_export_view](pics/spn_export_view.png)

**Playback** — воспроизведение принятых CAN-фреймов или собственной конфигурации (на базе J1939 DA или custom SPN)
![playback](pics/playback.png)

## Сборка

**Требования:**
- clang++ с поддержкой C++20
- CMake >= 3.13
- Ninja
- Системные библиотеки: boost (signals2, spirit, phoenix, regex), sqlite3, zlib, icu

Зависимости подтягиваются автоматически через CMake FetchContent:

- [FTXUI](https://github.com/ArthurSonzogni/FTXUI) — фреймворк терминального UI
- [tiny-process-library](https://gitlab.com/eidheim/tiny-process-library) — управление подпроцессами
- [sqlite_modern_cpp](https://github.com/SqliteModernCpp/sqlite_modern_cpp) — современная C++-обёртка над SQLite
- [xlnt](https://github.com/xlnt-community/xlnt) — чтение xlsx
- [lely-core](https://gitlab.com/lely_industries/lely-core) — стек CANopen
- [fmt](https://github.com/fmtlib/fmt) — форматирование текста
- [nlohmann/json](https://github.com/nlohmann/json) — JSON-библиотека
- [clipp](https://github.com/muellan/clipp) — парсинг CLI-аргументов

### Доступные таргеты

```bash
make list              # Показать все таргеты

make build             # Нативная сборка (динамическая линковка)
make build_static      # Нативная сборка (статическая линковка)
make install           # Установка в PREFIX (по умолчанию /usr/local), требует patchelf
make install_static    # Установка статического бинарника в PREFIX

make docker-run ARGS='...'       # Сборка и запуск в Docker (кросс-платформенно)
make build_arm64                 # Кросс-компиляция под arm64 (динамическая)
make build_arm64_static          # Кросс-компиляция под arm64 (статическая)

make clean             # Удалить все артефакты сборки
```

### Нативная сборка

```bash
make build
./build/native/canscope -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx
# или с CSV (быстрее парсится)
./build/native/canscope -e "candump can0" -j1939-csv thirdparty/j1939da_2018.csv
```

### Docker (кросс-платформенно)

Работает на Linux, macOS (?), Windows (?). Нужны только Docker и Make.

```bash
# TUI-режим — локальный CAN-интерфейс
make docker-run ARGS='-e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx'

# TUI-режим — удалённый CAN-интерфейс через SSH (если спросит пароль — данных не будет, используй ключ или sshpass)
make docker-run ARGS='-e "ssh user@remote candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx'

# Discover-режим — выяснить, какие PGN/SPN есть на шине
make docker-run ARGS='-discover -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx'

# Headless-режим — стрим декодированных значений
make docker-run ARGS='-hl -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx'
```

### Кросс-компиляция под arm64

```bash
make build_arm64           # динамическая линковка
make build_arm64_static    # статическая линковка
```

Нужен Docker. SSH-ключи из `~/.ssh` и `/etc/hosts` пробрасываются в контейнер сборки для выкачивания приватных git-зависимостей.

## Использование

Все режимы работы взаимоисключающи. Если ни один не указан — используется TUI.

```bash
# TUI-режим (по умолчанию) — интерактивный терминальный интерфейс
canscope -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx

# Discover-режим — вывод структуры PGN/SPN (без значений) в stdout или файл
canscope -discover -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx
canscope -discover -of discovered.json -e "candump can0" -j1939-csv thirdparty/j1939da_2018.csv

# Headless-режим — стрим всех декодированных значений (NDJSON) в stdout
canscope -hl -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx

# Record-режим — запись каждого фрейма в SQLite (per-frame, non-blocking)
canscope -rec -db recording.db -e "candump can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx

# Запись с kernel-таймстемпами (абсолютные unix-секунды через `-t a` или wall-clock через `-t A`)
canscope -rec -db recording.db -e "candump -t a can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx
canscope -rec -db recording.db -e "candump -t A can0" -j1939-xlsx thirdparty/j1939da_2018.xlsx

# Чтение из stdin (pipe)
candump can0 | canscope -j1939-csv thirdparty/j1939da_2018.csv
candump -t a can0 | canscope -rec -db recording.db -j1939-csv thirdparty/j1939da_2018.csv
```

> **Примечание:** декодирование J1939 тестировалось только с Digital Annex редакции 2018. Другие редакции могут работать, но без гарантии.

### Режимы

| Режим | Флаг | Описание |
|-------|------|----------|
| TUI | *(по умолчанию)* | Интерактивный полноэкранный терминальный UI |
| Discover | `-discover` | Вывод структуры PGN/SPN (без значений) в stdout или файл (`-of`) |
| Headless | `-hl` | Стрим всех декодированных значений PGN/SPN как NDJSON в stdout |
| Record | `-rec` | Запись всех декодированных значений PGN/SPN + таймстемпов в SQLite (`-db`) |

### CLI-флаги

| Флаг | Длинная форма | Описание |
|------|---------------|----------|
| `-j1939-xlsx` | | xlsx-файл J1939 Digital Annex |
| `-j1939-csv` | | csv-файл J1939 Digital Annex (быстрее парсится) |
| `-e` | `--execute-command` | Команда для чтения CAN-фреймов (например, `"candump can0"`) |
| `-discover` | | Discover-режим |
| `-hl` | `--headless` | Headless-режим |
| `-rec` | `--record` | Record-режим |
| `-of` | `--output-file` | Путь выходного файла (используется с `-discover`) |
| `-db` | `--database` | Путь к SQLite-БД (обязателен с `-rec`) |
| `-h` | `--help` | Показать справку |

### Формат записи

`-rec` пишет одну SQLite-БД с одной строкой на CAN-фрейм. Схема оптимизирована под размер на диске — сырые payload'ы не хранятся (только декодированные SPN в сжатом виде):

| Колонка | Тип     | Описание |
|---------|---------|----------|
| `id`    | INTEGER | PK, автоинкремент |
| `ts_ms` | INTEGER | Unix-миллисекунды. Берётся из `candump -t a`/`-t A`, если есть; иначе — системные часы на момент парсинга |
| `iface` | TEXT    | например, `can0` |
| `canid` | TEXT    | hex из 3 (SFF) или 8 (EFF) цифр |
| `pgn`   | INTEGER | J1939 PGN, `NULL` для не-J1939 фреймов или если Digital Annex не был передан |
| `spns`  | BLOB    | gzip JSON-массива. Каждый элемент: `{ts_ms, spn, name, value, unit}` |

Индексы: `ts_ms`, `(canid, ts_ms)`, `(pgn, ts_ms) WHERE pgn IS NOT NULL`. Открыта с `journal_mode=WAL`, `synchronous=NORMAL`. Вставки батчатся в транзакции по 1 секунде.

Пример быстрого запроса (для SQLite с JSON1 — gzip-BLOB надо сначала декодировать снаружи или вспомогательным скриптом):

```sql
SELECT ts_ms, canid, pgn FROM frames WHERE ts_ms BETWEEN ? AND ? ORDER BY ts_ms;
SELECT COUNT(*) FROM frames WHERE pgn = 61444;
```

## Roadmap

- **Поддержка NMEA 2000** — декодирование NMEA 2000 по базе PGN из [canboat](https://github.com/canboat/canboat) (JSON). Тот же 29-битный CAN ID, что и у J1939, требует имплементации Fast Packet
- **Поддержка CANopen** — декодирование CANopen параллельно с J1939 (11-битный CAN ID, SDO/PDO/NMT)
- **Мелкие улучшения** — улучшения UI, оптимизации производительности, дополнительные форматы экспорта
