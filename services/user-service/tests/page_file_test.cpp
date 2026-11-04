#include "tecflix/users/btree/page_file.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>
#include <string>

#include "tecflix/users/storage/storage_error.hpp"
#include "temp_directory.hpp"

namespace tecflix::users {
namespace {

constexpr std::uint32_t kDegree = 3;

class PageFileTest : public ::testing::Test {
 protected:
  std::filesystem::path IndexPath() const { return temp_.Path() / "id.idx"; }

  TempDirectory temp_;
};

TEST_F(PageFileTest, NewFileHasHeaderAndEmptyLeafRoot) {
  PageFile pages(IndexPath(), kDegree);

  EXPECT_EQ(pages.PageCount(), 2u);
  EXPECT_EQ(pages.RootPage(), 1u);
  EXPECT_EQ(std::filesystem::file_size(IndexPath()), 2 * PageFile::kPageSize);
  const BTreeNode root = pages.ReadNode(pages.RootPage());
  EXPECT_TRUE(root.is_leaf);
  EXPECT_TRUE(root.keys.empty());
}

TEST_F(PageFileTest, WrittenNodeSurvivesReopening) {
  std::uint64_t page = PageFile::kNoPage;
  {
    PageFile pages(IndexPath(), kDegree);
    BTreeNode node;
    node.page = pages.AllocatePage();
    node.keys = {BTreeKey::FromId(4)};
    node.values = {40};
    pages.WriteNode(node);
    pages.SetRootPage(node.page);
    page = node.page;
  }

  PageFile reopened(IndexPath(), kDegree);
  EXPECT_EQ(reopened.RootPage(), page);
  EXPECT_EQ(reopened.ReadNode(page).values, std::vector<std::uint64_t>({40}));
}

TEST_F(PageFileTest, AllocateAppendsNewPages) {
  PageFile pages(IndexPath(), kDegree);

  EXPECT_EQ(pages.AllocatePage(), 2u);
  EXPECT_EQ(pages.AllocatePage(), 3u);
  EXPECT_EQ(pages.PageCount(), 4u);
}

TEST_F(PageFileTest, FreedPagesAreReusedInLifoOrder) {
  PageFile pages(IndexPath(), kDegree);
  const auto first = pages.AllocatePage();
  const auto second = pages.AllocatePage();
  pages.FreePage(first);
  pages.FreePage(second);

  EXPECT_EQ(pages.AllocatePage(), second);
  EXPECT_EQ(pages.AllocatePage(), first);
  EXPECT_EQ(pages.AllocatePage(), 4u);
}

TEST_F(PageFileTest, FreeListSurvivesReopening) {
  std::uint64_t freed = PageFile::kNoPage;
  {
    PageFile pages(IndexPath(), kDegree);
    freed = pages.AllocatePage();
    pages.FreePage(freed);
  }

  PageFile reopened(IndexPath(), kDegree);
  EXPECT_EQ(reopened.AllocatePage(), freed);
}

TEST_F(PageFileTest, ReadingFreePageThrows) {
  PageFile pages(IndexPath(), kDegree);
  const auto page = pages.AllocatePage();
  pages.FreePage(page);

  EXPECT_THROW(pages.ReadNode(page), StorageError);
}

TEST_F(PageFileTest, RejectsInvalidPageNumbers) {
  PageFile pages(IndexPath(), kDegree);

  EXPECT_THROW(pages.ReadNode(0), StorageError);
  EXPECT_THROW(pages.ReadNode(99), StorageError);
  EXPECT_THROW(pages.FreePage(pages.RootPage()), std::logic_error);
}

TEST_F(PageFileTest, RejectsDegreeOutOfRange) {
  EXPECT_THROW(PageFile(IndexPath(), PageFile::kMinDegree - 1),
               std::invalid_argument);
  EXPECT_THROW(PageFile(IndexPath(), PageFile::kMaxDegree + 1),
               std::invalid_argument);
}

TEST_F(PageFileTest, RejectsReopeningWithAnotherDegree) {
  { PageFile pages(IndexPath(), kDegree); }

  EXPECT_THROW(PageFile(IndexPath(), kDegree + 1), StorageError);
}

TEST_F(PageFileTest, RejectsFileThatIsNotAnIndex) {
  std::ofstream(IndexPath(), std::ios::binary)
      << std::string(PageFile::kPageSize, 'x');

  EXPECT_THROW(PageFile(IndexPath(), kDegree), StorageError);
}

TEST_F(PageFileTest, MaxDegreeNodeFitsInOnePage) {
  PageFile pages(IndexPath(), PageFile::kMaxDegree);
  BTreeNode node;
  node.page = pages.AllocatePage();
  node.is_leaf = false;
  for (std::uint64_t i = 0; i < pages.MaxKeys(); ++i) {
    node.keys.push_back(BTreeKey::FromId(i));
    node.values.push_back(i);
    node.children.push_back(i + 1);
  }
  node.children.push_back(pages.MaxKeys() + 1);
  pages.WriteNode(node);

  EXPECT_EQ(pages.ReadNode(node.page).keys.size(), pages.MaxKeys());
  EXPECT_EQ(std::filesystem::file_size(IndexPath()),
            pages.PageCount() * PageFile::kPageSize);
}

}  // namespace
}  // namespace tecflix::users
