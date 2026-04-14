#include "headless_streamer.hpp"

#include <chrono>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

void HeadlessStreamer::onBatch(const std::vector<can_frame_update_s> &batch) {
  int64_t now =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count();

  for (const auto &u : batch) {
    if (!u.verbose || u.verbose->is_null() || !u.verbose->contains("SPNs")) {
      continue;
    }

    const auto &v = *u.verbose;
    nlohmann::json entry;

    entry["ts"] = now;
    entry["iface"] = u.iface;
    entry["canid"] = u.canid;

    if (v.contains("PGN")) {
      entry["pgn"] = v["PGN"];
    }

    if (v.contains("Label")) {
      entry["label"] = v["Label"];
    }

    nlohmann::json::array_t spns;
    for (const auto &spn : v["SPNs"]) {
      nlohmann::json s;

      if (spn.contains("SPN (integer)")) {
        s["spn"] = spn["SPN (integer)"];
      }

      if (spn.contains("SPN name")) {
        s["name"] = spn["SPN name"];
      }

      if (spn.contains("Value")) {
        s["value"] = spn["Value"];
      }

      if (spn.contains("Unit")) {
        s["unit"] = spn["Unit"];
      }

      spns.push_back(std::move(s));
    }

    entry["spns"] = std::move(spns);
    fmt::println("{}", entry.dump());
  }
}
