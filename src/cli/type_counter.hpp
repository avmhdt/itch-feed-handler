#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "itch/proto/messages.hpp"
#include "itch/util/time.hpp"

namespace itch::cli {

struct SessionEvent {
  char code;
  std::uint64_t timestamp_ns;
};

/// Parse-only handler: every message is decoded far enough to read its timestamp and is counted
/// under its type.
class TypeCounter {
 public:
  TypeCounter() { events_.reserve(kMaxEvents); }

  template <class View>
  void on(const View& message) noexcept {
    ++counts_[static_cast<unsigned char>(View::kType)];
    last_timestamp_ = message.timestamp_ns();
    if (!seen_) {
      seen_ = true;
      first_timestamp_ = last_timestamp_;
    }
  }

  /// System events are also kept, so that reports can show the session times (AC-W-10).
  void on(const SystemEvent& message) noexcept {
    if (events_.size() < events_.capacity()) {
      events_.push_back({message.event_code(), message.timestamp_ns()});
    }
    on<SystemEvent>(message);
  }

  [[nodiscard]] const std::array<std::uint64_t, 256>& counts() const noexcept { return counts_; }
  [[nodiscard]] const std::vector<SessionEvent>& events() const noexcept { return events_; }
  [[nodiscard]] std::uint64_t first_timestamp() const noexcept { return first_timestamp_; }
  [[nodiscard]] std::uint64_t last_timestamp() const noexcept { return last_timestamp_; }

 private:
  static constexpr std::size_t kMaxEvents = 16;

  std::array<std::uint64_t, 256> counts_{};
  std::vector<SessionEvent> events_;
  std::uint64_t first_timestamp_ = 0;
  std::uint64_t last_timestamp_ = 0;
  bool seen_ = false;
};

/// Stable, sorted-key JSON. Wall time goes to the log instead, so that two runs on the same input
/// produce byte-identical reports (NFR-04).
[[nodiscard]] std::string counts_json(const TypeCounter& counter, std::uint64_t messages,
                                      std::uint64_t malformed, std::size_t bytes,
                                      const std::optional<CivilDate>& date);

}  // namespace itch::cli
