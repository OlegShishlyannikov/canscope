#include <atomic>
#include <boost/signals2.hpp>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <future>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <ranges>
#include <stop_token>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/ranges.h>

// for XLSX files
#include "xlnt/xlnt.hpp"

// For sqlite
#include "sqlite_modern_cpp.h"

// For parsers
#include <boost/spirit/include/phoenix.hpp>
#include <boost/spirit/include/qi.hpp>

#include "bam_reassembler.hpp"
#include "candump_parser.hpp"
#include "discoverer.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "headless_streamer.hpp"
#include "process.hpp"
#include "recorder.hpp"
#include "signals.hpp"
#include "socketcan_reader.hpp"
#include <clipp.h>

std::mutex g_j1939_db_mtx;
std::atomic<uint64_t> g_error_frame_count{0};

int32_t main(int32_t argc, char *argv[]) {

  static auto screen = ftxui::ScreenInteractive::Fullscreen();
  static std::mutex rw_mtx;
  static std::map<std::string, std::map<std::string, std::shared_ptr<can_frame_data_s>>> aggregated;
  static signals_s signals;
  static std::atomic<sqlite::database *> j1939_db{nullptr};
  static std::stop_source aggregator_task_stop, refresh_task_stop, headless_task_stop;
  static TinyProcessLib::Process *p = nullptr;
  std::future<void> j1939_parser_task, headless_task;
  extern std::unique_ptr<sqlite::database> parseXlsx(const std::string &file);
  extern std::unique_ptr<sqlite::database> parseCsv(const std::string &file);
  static std::unique_ptr<sqlite::database> j1939_db_owner;
  static std::unique_ptr<Recorder> recorder;
  static std::unique_ptr<DiscovererHandler> discoverer_handler;
  static std::unique_ptr<HeadlessStreamer> headless_streamer;
  static std::shared_ptr<void> playback_handle;

  enum class Mode { tui, discover, record, headless, play } mode = Mode::tui;
  int32_t mode_flag_count = 0;

  static struct {
    std::string xlsx_file, csv_file, command = "", output_file = "", record_db_path = "", play_config_path = "";
    std::string can_ifaces;
    bool show_help = false;
    // Rotation defaults (used with -rec): 24-hour interval, keep 30 files.
    // Override via -rec-rotate-sec / -rec-rotate-keep. 0 disables.
    int64_t rec_rotate_seconds = 24LL * 3600;
    int64_t rec_rotate_keep = 30;
  } cli_opts;

  // Parse cli options
  {
    static const auto print_usage = []<typename Man>(const Man &man) {
      fmt::print("{}\r\n", (std::stringstream{} << man).str());
    };

    auto cli =
        (clipp::option("-dscvr, --discovery-mode")
             .doc("Discover mode: output PGN/SPN structure (only first received falue) to stdout or file")
             .call([&]() {
               mode = Mode::discover;
               ++mode_flag_count;
             }),

         clipp::option("-hl", "--headless")
             .doc("Headless mode: stream all decoded PGN/SPN values to stdout")
             .call([&]() {
               mode = Mode::headless;
               ++mode_flag_count;
             }),

         clipp::option("-rec", "--record")
             .doc("Record mode: write all decoded PGN/SPN values + timestamps to SQLite DB")
             .call([&]() {
               mode = Mode::record;
               ++mode_flag_count;
             }),

         clipp::option("-of", "--output-file") &
             clipp::value("Output file", cli_opts.output_file).doc("Output file path (used with -discover)"),

         clipp::option("-db", "--database") & clipp::value("SQLite output database path", cli_opts.record_db_path)
                                                  .doc("SQLite database path (used with -rec)"),

         clipp::option("-rec-rotate-sec", "--rotate-seconds") &
             clipp::value("Rotation interval in seconds", cli_opts.rec_rotate_seconds)
                 .doc("Rotation interval for -rec, in seconds. Default: 86400 (24h). 0 disables rotation."),

         clipp::option("-rec-rotate-keep", "--rotate-keep") &
             clipp::value("Max kept files", cli_opts.rec_rotate_keep)
                 .doc("Max number of rotated .db.gz files retained. Default: 30. 0 disables retention pruning."),

         (clipp::option("-play", "--playback-mode")
              .call([&]() {
                mode = Mode::play;
                ++mode_flag_count;
              })
              .doc("Play mode: send synthetic CAN frames per YAML config. "
                   "Optional path; defaults to /etc/canscope/playback.yaml. Exclusive with other modes.") &
          clipp::opt_value("Playback YAML config", cli_opts.play_config_path)),

         clipp::option("-e", "--execute-command") &
             clipp::value("command", cli_opts.command).call([&]() {}).doc("execute cli command to read its output"),

         clipp::option("-can", "--can-interfaces") &
             clipp::value("CAN interfaces", cli_opts.can_ifaces)
                 .doc("Read frames natively via SocketCAN (Linux only). "
                      "Comma-separated list, e.g. 'can0,can1' or 'any' for all. "
                      "Mutually exclusive with -e and stdin pipe."),

         (clipp::option("-j1939-xlsx") &
          clipp::value("J1939 XLSX file", cli_opts.xlsx_file)
              .call([&]() {
                j1939_parser_task = std::async(std::launch::async, [&]() {
                  j1939_db_owner = parseXlsx(cli_opts.xlsx_file);
                  j1939_db.store(j1939_db_owner.get());
                  signals.map.get<void(sqlite::database &)>("j1939_database_ready")->operator()(*j1939_db_owner);
                });
              })
              .doc("J1939 Digital Annex .xlsx file")) |

             (clipp::option("-j1939-csv") &
              clipp::value("J1939 CSV file", cli_opts.csv_file)
                  .call([&]() {
                    j1939_parser_task = std::async(std::launch::async, [&]() {
                      j1939_db_owner = parseCsv(cli_opts.csv_file);
                      j1939_db.store(j1939_db_owner.get());
                      signals.map.get<void(sqlite::database &)>("j1939_database_ready")->operator()(*j1939_db_owner);
                    });
                  })
                  .doc("J1939 Digital Annex .csv file")));

    auto cli_with_help = (cli | clipp::option("-h", "--help").set(cli_opts.show_help).doc("show this help"));
    auto man = clipp::make_man_page(cli_with_help, argv[0]);

    if (!clipp::parse(argc, argv, cli_with_help)) {
      print_usage(man);
      return -1;
    }

    if (cli_opts.show_help) {
      print_usage(man);
      return 0;
    }

    if (!cli_opts.can_ifaces.empty() && !cli_opts.command.empty()) {
      fmt::println(stderr, "Error: -can and -e are mutually exclusive (pick one frame source)");
      return -1;
    }

    if (mode == Mode::record && cli_opts.record_db_path.empty()) {
      fmt::println(stderr, "Error: -rec requires -db <path>");
      return -1;
    }

    if (mode_flag_count > 1) {
      fmt::println(stderr, "Error: modes -dscvr / -hl / -rec / -play are mutually exclusive");
      return -1;
    }

    if (mode == Mode::play && cli_opts.play_config_path.empty()) {
      cli_opts.play_config_path = "/etc/canscope/playback.yaml";
    }
  }

  // Parse a single candump line and aggregate it
  static const auto parse_candump_line = [](const std::string &line) {
    auto parsed = parseCandumpLine(line);
    switch (parsed.kind) {
    case parsed_candump_s::kind_e::invalid:
    case parsed_candump_s::kind_e::remote_frame:
      return;
    case parsed_candump_s::kind_e::error_frame:
      g_error_frame_count.fetch_add(1, std::memory_order_relaxed);
      return;
    case parsed_candump_s::kind_e::data:
      break;
    }

    auto raw = std::make_shared<raw_frame_s>(raw_frame_s{
        .ts_ms = parsed.ts_ms,
        .iface = parsed.iface,
        .canid = parsed.canid,
        .payload = parsed.payload,
    });

    auto reassembled = bamReassembler().feed(std::move(raw));
    for (const auto &f : reassembled) {
      can_frame_data_s entry;
      entry.payload = f->payload;
      entry.size = static_cast<int32_t>(f->payload.size());

      {
        std::lock_guard<std::mutex> lock(rw_mtx);
        aggregated[f->iface][f->canid] = std::make_shared<can_frame_data_s>(std::move(entry));
      }

      signals_s::map.get<void(const std::shared_ptr<const raw_frame_s> &)>("raw_frame")->operator()(f);
    }
  };

  const bool needs_input = (mode != Mode::play);
  const bool use_socketcan = !cli_opts.can_ifaces.empty();

  static int candump_fd = -1;
  if (needs_input && cli_opts.command.empty() && !use_socketcan) {
    candump_fd = ::dup(STDIN_FILENO);
    std::freopen("/dev/tty", "r", stdin);
  }

  // SocketCAN reader (Linux only). Stays valid across the lifetime of the
  // aggregator_task that owns its run-loop.
  static std::unique_ptr<SocketCanReader> can_reader;
  if (use_socketcan && needs_input) {
    try {
      can_reader = std::make_unique<SocketCanReader>(cli_opts.can_ifaces, true);
    } catch (const std::exception &e) {
      fmt::println(stderr, "Error: {}", e.what());
      return -1;
    }
  }

  // Reads candump data from stdin pipe / subprocess / SocketCAN and aggregates it
  std::future<void> aggregator_task;
  if (needs_input) {
    aggregator_task = std::async(
        std::launch::async,
        [command = cli_opts.command](std::stop_token stop_token) {
          if (can_reader) {
            can_reader->run(stop_token);
            return;
          }

          if (command.empty()) {

            // Read from the saved pipe fd using epoll to avoid blocking on stop
            int epfd = ::epoll_create1(0);
            if (epfd < 0) {
              return;
            }

            struct epoll_event ev = {
                .events = EPOLLIN,
                .data = {.fd = candump_fd},
            };

            ::epoll_ctl(epfd, EPOLL_CTL_ADD, candump_fd, &ev);

            FILE *pipe_stream = ::fdopen(candump_fd, "r");
            if (!pipe_stream) {
              ::close(epfd);
              return;
            }

            struct epoll_event events[1];
            char buf[4096];

            while (!stop_token.stop_requested()) {
              int32_t nfds = ::epoll_wait(epfd, events, 1, 50);
              if (nfds > 0 && !stop_token.stop_requested()) {
                if (events[0].events & EPOLLIN) {
                  if (!std::fgets(buf, sizeof(buf), pipe_stream)) {
                    break;
                  }

                  std::string line(buf);

                  if (!line.empty() && line.back() == '\n') {
                    line.pop_back();
                  }

                  parse_candump_line(line);
                }

                if (events[0].events & (EPOLLHUP | EPOLLERR)) {
                  break;
                }
              }
            }

            std::fclose(pipe_stream);
            ::close(epfd);
          } else {
            // Launch subprocess
            TinyProcessLib::Config cfg = {
                .buffer_size = PIPE_BUF,
                .inherit_file_descriptors = true,
                .on_stdout_close = []() {},
            };

            auto line_buf = std::make_shared<std::string>();
            p = new TinyProcessLib::Process(
                command, "",
                [stop_token, line_buf](const char *bytes, size_t n) {
                  if (n > PIPE_BUF || stop_token.stop_requested()) {
                    return;
                  }

                  line_buf->append(bytes, n);

                  size_t pos = 0;
                  while (true) {
                    size_t nl = line_buf->find('\n', pos);
                    if (nl == std::string::npos) {
                      break;
                    }
                    parse_candump_line(line_buf->substr(pos, nl - pos));
                    pos = nl + 1;
                  }
                  line_buf->erase(0, pos);
                },
                [](const char *, size_t) {}, false, cfg);

            while (true) {
              if (stop_token.stop_requested()) {
                if (p) {
                  p->kill();
                  ::kill(-p->get_id(), SIGKILL);
                  ::kill(p->get_id(), SIGKILL);
                  p->get_exit_status();
                  delete p;
                  p = nullptr;
                }

                break;
              }

              std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
          }
        },

        aggregator_task_stop.get_token());
  } // if (needs_input)

  // UI refresh task: compares snapshots at ~30fps and emits signals for changed entries
  std::future<void> refresh_task;
  if (needs_input)
    refresh_task = std::async(
        std::launch::async,
        [](std::stop_token stop_token) {
          using aggregated_t = std::map<std::string, std::map<std::string, std::shared_ptr<can_frame_data_s>>>;
          aggregated_t old_data;

          while (!stop_token.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(33u));

            aggregated_t current;

            {
              std::lock_guard<std::mutex> lock(rw_mtx);
              current = aggregated;
            }

            std::vector<can_frame_update_s> batch;

            for (const auto &[iface, canids] : current) {
              bool new_iface = !old_data.contains(iface);

              for (const auto &[canid, ptr] : canids) {
                if (!ptr) {
                  continue;
                }

                can_frame_diff_s diff;
                diff.is_new_interface = new_iface;
                diff.is_new_canid = new_iface || !old_data[iface].contains(canid);

                if (!diff.is_new_canid) {
                  const auto &old_ptr = old_data[iface][canid];
                  if (old_ptr == ptr) {
                    continue;
                  }

                  const auto &old_entry = *old_ptr;
                  diff.payload_changed.resize(ptr->payload.size(), false);

                  for (size_t i = 0; i < ptr->payload.size(); ++i) {
                    diff.payload_changed[i] =
                        (i >= old_entry.payload.size() || ptr->payload[i] != old_entry.payload[i]);
                  }
                } else {
                  diff.payload_changed.assign(ptr->payload.size(), true);
                }

                std::shared_ptr<nlohmann::json> verbose, brief;
                auto *db = j1939_db.load();
                if (db) {
                  std::lock_guard<std::mutex> db_lock(g_j1939_db_mtx);
                  extern std::pair<nlohmann::json, nlohmann::json> processFrame(
                      sqlite::database & db, const std::string &iface, const std::string &canid,
                      const std::vector<uint8_t> &data);
                  auto [v, b] = processFrame(*db, iface, canid, ptr->payload);

                  verbose = std::make_shared<nlohmann::json>(std::move(v));
                  brief = std::make_shared<nlohmann::json>(std::move(b));
                }

                batch.push_back({iface, canid, *ptr, std::move(diff), std::move(verbose), std::move(brief)});
              }
            }

            if (!batch.empty()) {
              signals.map.get<void(const std::vector<can_frame_update_s> &)>("new_entries_batch")->operator()(batch);
            }

            old_data.swap(current);
          }
        },

        refresh_task_stop.get_token());

  // Stop all tasks on SIGINT
  {
    static auto signal_handler = [](int sig) {
      for (auto *source : {&aggregator_task_stop, &refresh_task_stop, &headless_task_stop}) {
        if (!source->stop_requested()) {
          source->request_stop();
        }
      }

      if (candump_fd >= 0) {
        ::close(candump_fd);
        candump_fd = -1;
      }
    };

    ::signal(SIGINT, signal_handler);
  }

  if (mode == Mode::record) {
    const int64_t rotate_interval_ms = cli_opts.rec_rotate_seconds <= 0 ? 0 : cli_opts.rec_rotate_seconds * 1000;
    const size_t rotate_max_files = cli_opts.rec_rotate_keep <= 0 ? 0 : static_cast<size_t>(cli_opts.rec_rotate_keep);
    recorder = std::make_unique<Recorder>(cli_opts.record_db_path, true, rotate_interval_ms, rotate_max_files);
    signals.map.get<void(const std::shared_ptr<const raw_frame_s> &)>("raw_frame")
        ->connect([](const std::shared_ptr<const raw_frame_s> &frame) { recorder->pushFrame(frame); });
    signals.map.get<void(sqlite::database &)>("j1939_database_ready")->connect([](sqlite::database &db) {
      recorder->setJ1939Db(&db);
    });
  }

  if (mode == Mode::discover) {
    discoverer_handler = std::make_unique<DiscovererHandler>(cli_opts.output_file);

    signals.map.get<void(sqlite::database &)>("j1939_database_ready")->connect([](sqlite::database &db) {
      discoverer_handler->onDatabaseReady(db);
    });

    signals.map.get<void(const std::vector<can_frame_update_s> &)>("new_entries_batch")
        ->connect([](const std::vector<can_frame_update_s> &batch) { discoverer_handler->onBatch(batch); });
  }

  if (mode == Mode::headless) {
    headless_streamer = std::make_unique<HeadlessStreamer>();
    signals.map.get<void(const std::vector<can_frame_update_s> &)>("new_entries_batch")
        ->connect([](const std::vector<can_frame_update_s> &batch) { headless_streamer->onBatch(batch); });
  }

  if (mode == Mode::play) {
    extern std::shared_ptr<void> makePlayback(const std::string &yaml_path, sqlite::database &db, bool console_output);

    signals.map.get<void(sqlite::database &)>("j1939_database_ready")->connect([](sqlite::database &db) {
      try {
        playback_handle = makePlayback(cli_opts.play_config_path, db, true);
      } catch (const std::exception &e) {
        fmt::println(stderr, "Playback failed: {}", e.what());
        headless_task_stop.request_stop();
      }
    });
  }

  bool run_tui = (mode == Mode::tui);

  if (run_tui) {
    extern ftxui::Component makeMainForm(ftxui::ScreenInteractive * screen, signals_map_t & smap);
    screen.Loop(makeMainForm(&screen, signals.map) | ftxui::Renderer([](ftxui::Element inner) -> ftxui::Element {
                  return ftxui::Window(
                             {
                                 .inner = ftxui::Renderer([inner]() -> ftxui::Element { return inner | ftxui::flex; }),
                                 .title = "canscope",
                                 .width = ftxui::Terminal::Size().dimx,
                                 .height = ftxui::Terminal::Size().dimy,
                                 .resize_left = false,
                                 .resize_right = false,
                                 .resize_top = false,
                                 .resize_down = false,
                                 .render = [&](ftxui::WindowRenderState state) -> ftxui::Element {
                                   return ftxui::window(ftxui::Renderer([state]() {
                                                          return ftxui::text(fmt::format(" {{ {} }} ", state.title));
                                                        })->Render(),
                                                        state.inner);
                                 },
                             })
                      ->Render();
                }));

    signals.map.get<void()>("canplayer_stopped")->operator()();
    for (auto *source : {&aggregator_task_stop, &refresh_task_stop, &headless_task_stop}) {
      if (!source->stop_requested()) {
        source->request_stop();
      }
    }

    if (candump_fd >= 0) {
      ::close(candump_fd);
      candump_fd = -1;
    }
  } else {

    // Headless or rec-only: wait for SIGINT
    headless_task = std::async(
        std::launch::async,
        [](std::stop_token st) {
          while (!st.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
        },

        headless_task_stop.get_token());
  }

  // For non-TUI modes, block main on headless_task until SIGINT is received.
  // In TUI mode this task is never launched and we skip straight to cleanup.
  if (headless_task.valid()) {
    headless_task.wait();
  }

  // Cleanup grace period: once stop_sources have been requested (by SIGINT handler
  // or by TUI loop exit), give each task up to 3s to wind down before destruction.
  {
    const char *names[] = {
        "xlsx_parser",
        "aggregator",
        "refresh",
        "headless",
    };

    int idx = 0;
    for (auto *task : {
             &j1939_parser_task,
             &aggregator_task,
             &refresh_task,
             &headless_task,
         }) {
      if (task && task->valid()) {
        task->wait_for(std::chrono::seconds(3));
      }

      idx++;
    }
  }

  if (recorder) {
    fmt::println("Flushing recorded data to database, please wait...");
    recorder->flushAndClose();
    fmt::println("Done. Database saved to: {}", cli_opts.record_db_path);
    recorder.reset();
  }

  if (playback_handle) {
    playback_handle.reset(); // triggers Impl dtor -> stops sender tasks
  }

  return 0;
}
