// NFR-12: no heap allocation per message after warm-up. This target replaces global operator new
// with a counting one, so it is its own executable.

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <new>

#include "itch/book/book_manager.hpp"
#include "itch/io/file_driver.hpp"
#include "itch/io/mapped_file.hpp"

namespace {
std::atomic<std::uint64_t> g_allocations{0};
}  // namespace

// NOLINTBEGIN(misc-new-delete-overloads)
void* operator new(std::size_t size) {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  if (void* memory = std::malloc(size)) {
    return memory;
  }
  throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
// NOLINTEND(misc-new-delete-overloads)

namespace {

TEST(NoAlloc, SecondPassOverTheFixtureDayAllocatesNothing) {
  const itch::MappedFile file(std::filesystem::path(ITCH_FIXTURE_DIR) / "mini_day.itchbin");
  auto manager = std::make_unique<itch::BookManager>(
      itch::BookConfig{.order_capacity_hint = std::size_t{1} << 16});
  const auto no_progress = [](const itch::DriveResult&, std::uint64_t) {};

  const auto warm_up = itch::drive(file.bytes(), *manager, {}, no_progress);
  ASSERT_GT(warm_up.messages, 10'000U);
  const std::uint64_t digest = manager->digest();
  manager->reset();

  const std::uint64_t before = g_allocations.load();
  const auto second = itch::drive(file.bytes(), *manager, {}, no_progress);
  const std::uint64_t after = g_allocations.load();

  EXPECT_EQ(second.messages, warm_up.messages);
  EXPECT_EQ(after - before, 0U);
  EXPECT_EQ(manager->digest(), digest);
}

}  // namespace
