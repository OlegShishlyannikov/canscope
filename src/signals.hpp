#pragma once

#include <boost/signals2.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "can_data.hpp"

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#include "sqlite_modern_cpp.h"

class signals_map_s {
  struct signal_holder_base {
    virtual ~signal_holder_base() = default;
  };

  template <typename Signature>
  struct signal_holder : signal_holder_base {
    boost::signals2::signal<Signature> signal;
  };

public:
  template <typename Signature>
  void register_signal(const std::string &name) {
    signals_.emplace(name, std::make_unique<signal_holder<Signature>>());
  }

  template <typename Signature>
  auto *get(const std::string &name) {
    auto it = signals_.find(name);
    if (it == signals_.end()) {
      throw std::runtime_error(fmt::format("Signal '{}' not found", name));
    }
    auto *holder = dynamic_cast<signal_holder<Signature> *>(it->second.get());
    if (!holder) {
      throw std::runtime_error(fmt::format("Signal '{}' type mismatch", name));
    }
    return &holder->signal;
  }

  template <typename Signature>
  auto *get(const char *name) {
    return get<Signature>(std::string(name));
  }

private:
  std::unordered_map<std::string, std::unique_ptr<signal_holder_base>> signals_;
};

struct signals_s {
  static inline signals_map_s map = []() {
    signals_map_s m;
    m.register_signal<void(const std::string &)>("new_data_recvd");
    m.register_signal<void(const std::string &, const std::string &, const can_frame_data_s &, const can_frame_diff_s &)>("new_entry");
    m.register_signal<void(const std::vector<can_frame_update_s> &)>("new_entries_batch");
    m.register_signal<void(const std::string &)>("show_settings");
    m.register_signal<void()>("show_file_dialog_request");
    m.register_signal<void(const std::string &)>("export_file_request");
    m.register_signal<void(const std::string &, size_t)>("new_tag_request");
    m.register_signal<void(sqlite::database &)>("j1939_database_ready");
    m.register_signal<void()>("canplayer_started");
    m.register_signal<void()>("canplayer_stopped");
    return m;
  }();
};

using signals_map_t = signals_map_s;
