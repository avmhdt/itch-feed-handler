#include "itch/util/config.hpp"

#include <gtest/gtest.h>
#include <stdlib.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "itch/out/parquet_sink.hpp"
#include "itch/util/spsc_queue.hpp"

namespace {

class ConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    path_ = std::filesystem::temp_directory_path() / (std::string("itch_cfg_") + info->name());
    setenv("ITCH_TEST_ROOT", "/tmp/root", 1);
  }
  void TearDown() override { std::filesystem::remove(path_); }

  void write(const std::string& text) const { std::ofstream(path_) << text; }

  std::filesystem::path path_;
};

TEST_F(ConfigTest, ExpandEnvReplacesEachVariable) {
  EXPECT_EQ(itch::expand_env("${ITCH_TEST_ROOT}/a/${ITCH_TEST_ROOT}"), "/tmp/root/a//tmp/root");
  EXPECT_EQ(itch::expand_env("plain"), "plain");
  EXPECT_THROW(static_cast<void>(itch::expand_env("${ITCH_TEST_UNSET_NAME}")), std::runtime_error);
  EXPECT_THROW(static_cast<void>(itch::expand_env("${ITCH_TEST_ROOT")), std::runtime_error);
}

TEST_F(ConfigTest, ReadsEveryField) {
  write(R"(
[input]
path = "${ITCH_TEST_ROOT}/day"
date = "2019-03-27"
strict = false
[symbols]
watchlist_file = "w.txt"
outputs_for = "all"
[outputs]
root = "${ITCH_TEST_ROOT}/out"
venue = "XTST"
datasets = ["trades", "bars"]
compression_level = 5
row_group_rows = 1_000
queue_capacity = 128
[bars]
intervals = ["5s"]
session = "rth"
include_cross = false
[snapshots]
interval = "5m"
depth = 3
[checks]
crossed_book = "fail"
[book]
order_capacity_hint = 99
order_map = "std"
)");
  const itch::Config config = itch::load_config(path_);
  EXPECT_EQ(config.input_path, "/tmp/root/day");
  EXPECT_EQ(config.input_date, "2019-03-27");
  EXPECT_FALSE(config.strict);
  EXPECT_EQ(config.watchlist_file, "w.txt");
  EXPECT_EQ(config.outputs_for, "all");
  EXPECT_EQ(config.outputs_root, "/tmp/root/out");
  EXPECT_EQ(config.venue, "XTST");
  EXPECT_EQ(config.datasets.size(), 2U);
  EXPECT_EQ(config.compression_level, 5);
  EXPECT_EQ(config.row_group_rows, 1000);
  EXPECT_EQ(config.queue_capacity, 128U);
  EXPECT_EQ(config.bar_intervals, std::vector<std::string>{"5s"});
  EXPECT_EQ(config.bar_session, "rth");
  EXPECT_FALSE(config.bar_include_cross);
  EXPECT_EQ(config.snapshot_interval, "5m");
  EXPECT_EQ(config.snapshot_depth, 3U);
  EXPECT_EQ(config.crossed_book, "fail");
  EXPECT_EQ(config.order_capacity_hint, 99U);
  EXPECT_EQ(config.order_map, "std");
}

TEST_F(ConfigTest, MissingFieldsKeepTheirDefaults) {
  write("[input]\npath = \"x\"\n");
  const itch::Config config = itch::load_config(path_);
  EXPECT_TRUE(config.strict);
  EXPECT_EQ(config.outputs_for, "watchlist");
  EXPECT_EQ(config.row_group_rows, 1'000'000);
  EXPECT_EQ(config.bar_intervals.size(), 2U);
  EXPECT_TRUE(config.datasets.empty());
}

TEST_F(ConfigTest, BadTomlIsAnError) {
  write("[input\n");
  EXPECT_THROW(static_cast<void>(itch::load_config(path_)), std::runtime_error);
}

TEST_F(ConfigTest, DefaultConfigLoads) {
  setenv("ITCH_DATA_DIR", "/tmp/data", 1);
  const itch::Config config =
      itch::load_config(std::filesystem::path(ITCH_REPO_DIR) / "configs" / "default.toml");
  EXPECT_EQ(config.input_date, "2019-01-30");
  EXPECT_EQ(config.datasets.size(), 8U);
  EXPECT_EQ(itch::dataset_mask(config.datasets), 0xFFU);
  EXPECT_EQ(config.order_capacity_hint, 50'000'000U);
}

TEST_F(ConfigTest, WatchlistSkipsCommentsAndBlankLines) {
  write("# heading\nAAPL\n\nMSFT\n");
  EXPECT_EQ(itch::read_watchlist(path_), (std::vector<std::string>{"AAPL", "MSFT"}));
  EXPECT_THROW(static_cast<void>(itch::read_watchlist(path_ / "missing")), std::runtime_error);
}

TEST(Datasets, UnknownNameIsAnError) {
  EXPECT_THROW(static_cast<void>(itch::dataset_mask({"book_l2"})), std::invalid_argument);
}

TEST(SpscQueue, KeepsOrderAcrossWrapAndReportsFull) {
  itch::SpscQueue<int> queue(3);
  EXPECT_EQ(queue.capacity(), 4U);
  int value = 0;
  EXPECT_FALSE(queue.try_pop(value));
  for (int round = 0; round < 3; ++round) {
    for (int i = 0; i < 4; ++i) {
      EXPECT_TRUE(queue.try_push(i));
    }
    EXPECT_FALSE(queue.try_push(9));
    for (int i = 0; i < 4; ++i) {
      ASSERT_TRUE(queue.try_pop(value));
      EXPECT_EQ(value, i);
    }
  }
}

TEST(SpscQueue, DeliversEverythingAcrossThreads) {
  itch::SpscQueue<int> queue(16);
  constexpr int kCount = 100'000;
  std::thread producer([&queue] {
    for (int i = 0; i < kCount; ++i) {
      while (!queue.try_push(i)) {
        std::this_thread::yield();
      }
    }
  });
  int expected = 0;
  while (expected < kCount) {
    int value = 0;
    if (queue.try_pop(value)) {
      ASSERT_EQ(value, expected);
      ++expected;
    }
  }
  producer.join();
}

}  // namespace
