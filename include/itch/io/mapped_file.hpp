#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

namespace itch {

/// Read-only memory mapping of a whole file (FR-01). The page cache does the read-ahead, so a
/// multi-GB day streams with no copies. Throws std::system_error when the file cannot be mapped.
class MappedFile {
 public:
  explicit MappedFile(const std::filesystem::path& path);
  ~MappedFile();

  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;

  [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return {data_, size_}; }

 private:
  const std::byte* data_ = nullptr;
  std::size_t size_ = 0;
};

}  // namespace itch
