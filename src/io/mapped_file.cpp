#include "itch/io/mapped_file.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <system_error>

namespace itch {

namespace {

[[noreturn]] void fail(int fd, const std::filesystem::path& path) {
  const int error = errno;
  if (fd >= 0) {
    ::close(fd);
  }
  throw std::system_error(error, std::generic_category(), path.string());
}

}  // namespace

MappedFile::MappedFile(const std::filesystem::path& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    fail(fd, path);
  }
  struct stat info {};
  if (::fstat(fd, &info) != 0) {
    fail(fd, path);
  }
  size_ = static_cast<std::size_t>(info.st_size);
  if (size_ > 0) {
    void* mapped = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
      fail(fd, path);
    }
    ::madvise(mapped, size_, MADV_SEQUENTIAL);
    data_ = static_cast<const std::byte*>(mapped);
  }
  ::close(fd);
}

MappedFile::~MappedFile() {
  if (data_ != nullptr) {
    ::munmap(const_cast<std::byte*>(data_), size_);
  }
}

}  // namespace itch
