#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "itch/io/frame_reader.hpp"
#include "itch/proto/parse.hpp"

namespace itch {

struct DriveOptions {
  bool strict = true;       ///< DQ-01: stop at the first malformed frame instead of skipping it.
  std::uint64_t limit = 0;  ///< Stop after this many frames; 0 means the whole input.
  std::uint64_t progress_every = 50'000'000;  ///< NFR-10 cadence, in messages.
  /// DQ-11: stop before the first message later than this time, nanoseconds since midnight.
  std::uint64_t until_timestamp_ns = std::numeric_limits<std::uint64_t>::max();
};

struct DriveResult {
  std::uint64_t messages = 0;   ///< Frames read, malformed ones included.
  std::uint64_t malformed = 0;  ///< Frames with an unknown type or a wrong length.
  std::uint64_t offset = 0;     ///< Next unread byte, or the failing frame when stopped.
  bool truncated = false;       ///< DQ-02: a length prefix ran past the end of the input.
  bool stopped = false;         ///< Strict mode stopped on a malformed frame.
};

inline constexpr std::uint64_t kNoTimeLimit = std::numeric_limits<std::uint64_t>::max();
inline constexpr std::size_t kHeaderBytes = 11;

/// Feeds every frame of `data` to `handler`. `progress` runs between messages at the
/// `progress_every` cadence, so logging stays out of the per-message path.
template <MessageHandler H, class Progress>
DriveResult drive(std::span<const std::byte> data, H& handler, const DriveOptions& options,
                  Progress&& progress) {
  DriveResult result;
  FrameReader reader(data);
  std::span<const std::byte> frame;
  std::uint64_t until_progress = options.progress_every;

  while (options.limit == 0 || result.messages < options.limit) {
    const std::uint64_t frame_offset = reader.offset();
    const FrameReader::Next next = reader.next(frame);
    if (next != FrameReader::Next::Frame) {
      result.truncated = next == FrameReader::Next::Truncated;
      break;
    }
    if (options.until_timestamp_ns != kNoTimeLimit && frame.size() >= kHeaderBytes &&
        load_be48(frame.data() + 5) > options.until_timestamp_ns) {
      result.offset = frame_offset;
      return result;
    }
    ++result.messages;
    if (parse(frame, handler) != ParseStatus::Ok) {
      ++result.malformed;
      if (options.strict) {
        result.stopped = true;
        result.offset = frame_offset;
        return result;
      }
    }
    if (--until_progress == 0) {
      until_progress = options.progress_every;
      progress(result, reader.offset());
    }
  }
  result.offset = reader.offset();
  return result;
}

}  // namespace itch
