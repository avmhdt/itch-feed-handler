#include "itch/io/mapped_file.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

class MappedFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    path_ = std::filesystem::temp_directory_path() / (std::string("itch_map_") + info->name());
  }
  void TearDown() override { std::filesystem::remove(path_); }

  std::filesystem::path path_;
};

TEST_F(MappedFileTest, MapsFileContents) {
  std::ofstream(path_, std::ios::binary) << "hello";
  const itch::MappedFile file(path_);
  ASSERT_EQ(file.bytes().size(), 5U);
  EXPECT_EQ(std::to_integer<char>(file.bytes()[0]), 'h');
  EXPECT_EQ(std::to_integer<char>(file.bytes()[4]), 'o');
}

TEST_F(MappedFileTest, EmptyFileGivesEmptySpan) {
  std::ofstream(path_, std::ios::binary).flush();
  const itch::MappedFile file(path_);
  EXPECT_TRUE(file.bytes().empty());
}

TEST_F(MappedFileTest, MissingFileThrows) {
  EXPECT_THROW(itch::MappedFile{path_ / "missing"}, std::system_error);
}

TEST_F(MappedFileTest, UnmappableFileThrows) {
  EXPECT_THROW(itch::MappedFile{std::filesystem::temp_directory_path()}, std::system_error);
}

}  // namespace
