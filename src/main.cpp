#include <atomic>
#include <boost/signals2.hpp>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <future>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
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

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "headless.hpp"
#include "process.hpp"
#include "recorder.hpp"
#include "signals.hpp"
#include <clipp.h>

std::mutex g_j1939_db_mtx;

int32_t main(int32_t argc, char *argv[]) {
  static auto screen = ftxui::ScreenInteractive::Fullscreen();
  static std::mutex rw_mtx;
  static std::map<std::string, std::map<std::string, std::shared_ptr<can_frame_data_s>>> aggregated;
  static signals_s signals;
  static std::atomic<sqlite::database *> j1939_db{nullptr};
  static std::stop_source aggregator_task_stop, refresh_task_stop, headless_task_stop;
  static TinyProcessLib::Process *p = nullptr;
  std::future<void> xlsx_parser_task, headless_task;
  extern std::unique_ptr<sqlite::database> parseXlsx(const std::string &file);
  static std::unique_ptr<sqlite::database> j1939_db_owner;
  static std::unique_ptr<Recorder> recorder;
  static std::unique_ptr<HeadlessHandler> headless_handler;

  static struct {
    std::string docfile, command = "", output_file = "", record_db_path = "";
    bool show_help = false, headless_mode = false, sync_to_server = false, record_mode = false, tui_mode = false;
  } cli_opts;

  // Parse cli options
  {
    static const auto print_usage = []<typename Man>(const Man &man) {
      fmt::print("{}\r\n", (std::stringstream{} << man).str());
    };

    auto cli = (

        clipp::option("-hl", "--headless")
            .doc("Headless mode, write configuration to stdout if output file is not specified")
            .set(cli_opts.headless_mode)
            .call([]() { fmt::println("Headless mode"); }),

        clipp::option("-of", "--output-file") &
            clipp::value("Output file to write configuration", cli_opts.output_file)
                .call([&]() { fmt::println("Output file is: {}", cli_opts.output_file); })
                .doc("Specify output file to write configuration"),

        clipp::option("-rec", "--record")
            .doc("Record mode: collect decoded J1939 SPN values to SQLite DB")
            .set(cli_opts.record_mode)
            .call([]() { fmt::println("Record mode"); }),

        clipp::option("-db", "--database") &
            clipp::value("SQLite output database path", cli_opts.record_db_path)
                .call([&]() { fmt::println("Record DB: {}", cli_opts.record_db_path); })
                .doc("Path for the output SQLite database (used with -rec)"),

        clipp::option("-tui").doc("Enable TUI mode (use with -rec to show UI while recording)").set(cli_opts.tui_mode),

        clipp::option("-e", "--execute-command") &
            clipp::value("command", cli_opts.command).call([&]() {}).doc("execute cli command to read its output"),

        clipp::required("-j1939", "--j1939-document") &
            clipp::value("J1939 Document file", cli_opts.docfile)
                .call([&]() {
                  xlsx_parser_task = std::async(std::launch::async, [&]() {
                    j1939_db_owner = parseXlsx(cli_opts.docfile);
                    j1939_db.store(j1939_db_owner.get());
                    signals.map.get<void(sqlite::database &)>("j1939_database_ready")->operator()(*j1939_db_owner);
                  });
                })
                .doc("provide .xlsx document file for J1939 processing"));

    auto man = clipp::make_man_page(cli, argv[0]);
    auto cli_with_help = (cli | clipp::option("-h", "--help").set(cli_opts.show_help).doc("show this help").call([&]() {
      print_usage(man);
    }));

    if (!clipp::parse(argc, argv, cli_with_help)) {
      print_usage(man);
      return -1;
    }

    if (cli_opts.record_mode && cli_opts.record_db_path.empty()) {
      fmt::println(stderr, "Error: -rec requires -db <path>");
      return -1;
    }
  }

  // Parse a single candump line and aggregate it
  static const auto parse_candump_line = [](const std::string &line) {
    if (line.empty())
      return;

    std::vector<std::string> words;
    std::istringstream iss(line);
    std::string s;

    while (std::getline(iss, s, ' ')) {
      if (!s.empty()) {
        words.push_back(s);
      }
    }

    if (words.size() >= 4u) { // 0 - interface, 1 - canid, 2 - size, 3 - payload (1 byte minimum)
      if (words[0].starts_with("can") || words[0].starts_with("vcan")) {
        can_frame_data_s entry;

        // Parse DLC
        {
          auto sv = std::string_view(words[2]).substr(1, words[2].size() - 2);
          auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), entry.size);
          if (ec != std::errc{}) {
            entry.size = 0;
          }
        }

        // Parse payload bytes directly
        entry.payload.reserve(words.size() - 3);
        for (size_t i = 3; i < words.size(); ++i) {
          uint8_t byte = 0;
          std::from_chars(words[i].data(), words[i].data() + words[i].size(), byte, 16);
          entry.payload.push_back(byte);
        }

        {
          std::lock_guard<std::mutex> lock(rw_mtx);
          aggregated[words[0]][words[1]] = std::make_shared<can_frame_data_s>(std::move(entry));
        }
      }
    }
  };

  // If reading from stdin pipe, save the pipe fd and reopen stdin as /dev/tty for FTXUI
  static int candump_fd = -1;
  if (cli_opts.command.empty()) {
    candump_fd = ::dup(STDIN_FILENO);
    std::freopen("/dev/tty", "r", stdin);
  }

  // Reads candump data from stdin pipe or subprocess and aggregates it
  auto aggregator_task = std::async(
      std::launch::async,
      [command = cli_opts.command](std::stop_token stop_token) {
        if (command.empty()) {

          // Read from the saved pipe fd using epoll to avoid blocking on stop
          int epfd = ::epoll_create1(0);
          if (epfd < 0)
            return;

          struct epoll_event ev = {.events = EPOLLIN, .data = {.fd = candump_fd}};
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
              .buffer_size = PIPE_BUF, .inherit_file_descriptors = true, .on_stdout_close = []() {}};
          p = new TinyProcessLib::Process(
              command, "",
              [stop_token](const char *bytes, size_t n) {
                if (n > PIPE_BUF || stop_token.stop_requested())
                  return;

                std::string buf(bytes, n), line;
                std::istringstream ss(buf);
                while (std::getline(ss, line)) {
                  parse_candump_line(line);
                }
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

  // UI refresh task: compares snapshots at ~30fps and emits signals for changed entries
  auto refresh_task = std::async(
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
              if (!ptr)
                continue;

              can_frame_diff_s diff;
              diff.is_new_interface = new_iface;
              diff.is_new_canid = new_iface || !old_data[iface].contains(canid);

              if (!diff.is_new_canid) {
                const auto &old_ptr = old_data[iface][canid];
                if (old_ptr == ptr)
                  continue;

                const auto &old_entry = *old_ptr;
                diff.payload_changed.resize(ptr->payload.size(), false);

                for (size_t i = 0; i < ptr->payload.size(); ++i) {
                  diff.payload_changed[i] = (i >= old_entry.payload.size() || ptr->payload[i] != old_entry.payload[i]);
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

  if (cli_opts.record_mode) {
    bool rec_console = !cli_opts.tui_mode && !cli_opts.headless_mode;
    recorder = std::make_unique<Recorder>(cli_opts.record_db_path, rec_console);
    signals.map.get<void(const std::vector<can_frame_update_s> &)>("new_entries_batch")
        ->connect([](const std::vector<can_frame_update_s> &batch) { recorder->onBatch(batch); });
  }

  if (cli_opts.headless_mode) {
    headless_handler = std::make_unique<HeadlessHandler>(cli_opts.output_file);

    signals.map.get<void(sqlite::database &)>("j1939_database_ready")->connect([](sqlite::database &db) {
      headless_handler->onDatabaseReady(db);
    });

    signals.map.get<void(const std::vector<can_frame_update_s> &)>("new_entries_batch")
        ->connect([](const std::vector<can_frame_update_s> &batch) { headless_handler->onBatch(batch); });
  }

  // Determine if TUI should run: default on, off if headless, off if rec-only (no -tui)
  bool run_tui = !cli_opts.headless_mode && (!cli_opts.record_mode || cli_opts.tui_mode);

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

  // Wait for all tasks to finish (with timeout)
  {
    const char *names[] = {"xlsx_parser", "aggregator", "refresh", "headless"};
    int idx = 0;
    for (auto *task : {&xlsx_parser_task, &aggregator_task, &refresh_task, &headless_task}) {
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

  return 0;
}
