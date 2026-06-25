#include "socketcan_reader.hpp"

#include <stdexcept>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

#ifdef __linux__

#  include "bam_reassembler.hpp"
#  include "can_data.hpp"
#  include "signals.hpp"

#  include <atomic>
#  include <chrono>
#  include <cstdint>
#  include <cstring>
#  include <memory>
#  include <sstream>
#  include <string_view>
#  include <sys/epoll.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>

#  include <linux/can.h>
#  include <linux/can/error.h>
#  include <linux/can/raw.h>
#  include <net/if.h>

namespace {

// Split "can0,can1,any" → {"can0", "can1", "any"}. Whitespace around items
// stripped; empty items skipped. If "any" appears anywhere, return {"any"}
// since the SocketCAN "any" pseudo-interface already covers every CAN bus.
std::vector<std::string> splitInterfaces(const std::string &csv) {
  std::vector<std::string> out;
  std::stringstream ss(csv);
  std::string item;
  while (std::getline(ss, item, ',')) {
    // trim whitespace
    size_t a = item.find_first_not_of(" \t");
    size_t b = item.find_last_not_of(" \t");
    if (a == std::string::npos) {
      continue;
    }
    std::string trimmed = item.substr(a, b - a + 1);
    if (trimmed == "any") {
      return {"any"};
    }
    out.push_back(std::move(trimmed));
  }
  if (out.empty()) {
    throw std::runtime_error("SocketCAN: interface list is empty");
  }
  return out;
}

int openSocketForIface(const std::string &name) {
  int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (fd < 0) {
    throw std::runtime_error(fmt::format("SocketCAN: socket() failed: {}", std::strerror(errno)));
  }

  // Receive error frames so the error-frame counter mirrors candump's behaviour.
  can_err_mask_t err_mask = CAN_ERR_MASK;
  if (::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask)) < 0) {
    // non-fatal: error frames just won't be reported
  }

  // Kernel timestamps (millisecond-ish) attached as SCM_TIMESTAMP control message.
  int on = 1;
  if (::setsockopt(fd, SOL_SOCKET, SO_TIMESTAMP, &on, sizeof(on)) < 0) {
    // non-fatal: we'll fall back to wall clock
  }

  sockaddr_can addr{};
  addr.can_family = AF_CAN;
  if (name == "any") {
    addr.can_ifindex = 0; // 0 = bind to all interfaces
  } else {
    addr.can_ifindex = static_cast<int>(::if_nametoindex(name.c_str()));
    if (addr.can_ifindex == 0) {
      ::close(fd);
      throw std::runtime_error(fmt::format("SocketCAN: interface '{}' not found", name));
    }
  }

  if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    const std::string err = std::strerror(errno);
    ::close(fd);
    throw std::runtime_error(fmt::format("SocketCAN: bind '{}' failed: {}", name, err));
  }

  return fd;
}

// Format raw 32-bit canid (bits 0-28) as 3 hex digits for SFF, 8 for EFF —
// matches the textual format used by the candump-source path so downstream
// parsing/storage is consistent.
std::string formatCanId(canid_t id, bool eff) {
  return eff ? fmt::format("{:08X}", id & CAN_EFF_MASK) : fmt::format("{:03X}", id & CAN_SFF_MASK);
}

int64_t epochMsNow() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

} // namespace

SocketCanReader::SocketCanReader(const std::string &ifaces, bool console_output) : m_console_output_(console_output) {
  auto names = splitInterfaces(ifaces);
  m_fds_.reserve(names.size());
  try {
    for (const auto &name : names) {
      m_fds_.push_back(openSocketForIface(name));
    }
  } catch (...) {
    for (int fd : m_fds_) {
      ::close(fd);
    }
    m_fds_.clear();
    throw;
  }
}

SocketCanReader::~SocketCanReader() {
  for (int fd : m_fds_) {
    ::close(fd);
  }
}

void SocketCanReader::run(std::stop_token st) {
  int epfd = ::epoll_create1(0);
  if (epfd < 0) {
    if (m_console_output_) {
      fmt::println(stderr, "SocketCAN: epoll_create1 failed: {}", std::strerror(errno));
    }
    return;
  }
  auto epoll_close = std::unique_ptr<int, void (*)(int *)>(&epfd, [](int *p) { ::close(*p); });

  for (int fd : m_fds_) {
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
      if (m_console_output_) {
        fmt::println(stderr, "SocketCAN: epoll_ctl failed: {}", std::strerror(errno));
      }
      return;
    }
  }

  epoll_event events[16];
  can_frame frame;
  sockaddr_can src_addr;
  char ctrl[CMSG_SPACE(sizeof(timeval))];

  while (!st.stop_requested()) {
    const int n = ::epoll_wait(epfd, events, 16, 100); // 100 ms wakeup to re-check stop_token
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (m_console_output_) {
        fmt::println(stderr, "SocketCAN: epoll_wait failed: {}", std::strerror(errno));
      }
      break;
    }

    for (int i = 0; i < n; ++i) {
      const int fd = events[i].data.fd;
      iovec iov{};
      iov.iov_base = &frame;
      iov.iov_len = sizeof(frame);

      msghdr msg{};
      msg.msg_name = &src_addr;
      msg.msg_namelen = sizeof(src_addr);
      msg.msg_iov = &iov;
      msg.msg_iovlen = 1;
      msg.msg_control = ctrl;
      msg.msg_controllen = sizeof(ctrl);

      const ssize_t got = ::recvmsg(fd, &msg, 0);
      if (got < 0) {
        if (errno == EINTR || errno == EAGAIN) {
          continue;
        }
        if (m_console_output_) {
          fmt::println(stderr, "SocketCAN: recvmsg failed: {}", std::strerror(errno));
        }
        continue;
      }
      if (got != sizeof(can_frame)) {
        continue; // ignore partial / non-CAN reads
      }

      // Error frame: bump counter, drop. Same semantics as the candump-text
      // path's ERRORFRAME marker.
      if (frame.can_id & CAN_ERR_FLAG) {
        g_error_frame_count.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      // RTR (remote): drop silently — we don't have payload to record.
      if (frame.can_id & CAN_RTR_FLAG) {
        continue;
      }

      // Pull the kernel timestamp out of the control-message stream;
      // fall back to wall clock if SO_TIMESTAMP wasn't honoured.
      int64_t ts_ms = 0;
      for (cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMP) {
          timeval tv;
          std::memcpy(&tv, CMSG_DATA(cmsg), sizeof(tv));
          ts_ms = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
          break;
        }
      }
      if (ts_ms == 0) {
        ts_ms = epochMsNow();
      }

      // Resolve interface name from ifindex (set by SocketCAN on recvmsg).
      char ifname[IF_NAMESIZE]{};
      if (src_addr.can_ifindex != 0 && ::if_indextoname(src_addr.can_ifindex, ifname) == nullptr) {
        ifname[0] = '\0';
      }

      const bool eff = (frame.can_id & CAN_EFF_FLAG) != 0;
      auto raw = std::make_shared<raw_frame_s>(raw_frame_s{
          .ts_ms = ts_ms,
          .iface = ifname[0] ? std::string(ifname) : std::string("can"),
          .canid = formatCanId(frame.can_id, eff),
          .payload = std::vector<uint8_t>(frame.data, frame.data + frame.can_dlc),
      });

      // Funnel through the BAM reassembler — same contract as the
      // candump-text path. Transport frames are swallowed, the
      // reassembled virtual frame is emitted on the final TP.DT.
      for (const auto &f : bamReassembler().feed(std::move(raw))) {
        signals_s::map.get<void(const std::shared_ptr<const raw_frame_s> &)>("raw_frame")->operator()(f);
      }
    }
  }
}

#else // !__linux__

SocketCanReader::SocketCanReader(const std::string &, bool) {
  throw std::runtime_error("SocketCAN is only supported on Linux. "
                           "Use -e \"candump <iface>\" to read CAN frames on this platform.");
}

SocketCanReader::~SocketCanReader() = default;

void SocketCanReader::run(std::stop_token) {}

#endif
