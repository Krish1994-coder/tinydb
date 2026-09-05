#include "tinydb/storage/storage_manager.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

namespace tinydb {
namespace {

class StorageManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ =
            std::filesystem::temp_directory_path() /
            ("tinydb_storage_test_" +
             std::to_string(::getpid()) +
             "_" +
             std::to_string(counter_++) +
             ".tdb");
    }

    void TearDown() override {
        std::filesystem::remove(path_);
    }

    std::filesystem::path path_;
    static inline int counter_ = 0;
};

TEST_F(StorageManagerTest, CreateDatabase) {
    StorageManager sm;

    EXPECT_FALSE(sm.IsOpen());
    EXPECT_EQ(sm.CreateDatabase(path_.string()), StatusCode::kOk);
    EXPECT_TRUE(sm.IsOpen());
    EXPECT_EQ(sm.GetPageSize(), 4096u);
    EXPECT_EQ(sm.GetPageCount(), 1u);

    EXPECT_EQ(sm.CloseDatabase(), StatusCode::kOk);
    EXPECT_FALSE(sm.IsOpen());
}

TEST_F(StorageManagerTest, CreateExistingDatabaseReturnsAlreadyExists) {
    StorageManager sm;

    ASSERT_EQ(sm.CreateDatabase(path_.string()), StatusCode::kOk);
    ASSERT_EQ(sm.CloseDatabase(), StatusCode::kOk);

    StorageManager second;
    EXPECT_EQ(second.CreateDatabase(path_.string()),
              StatusCode::kAlreadyExists);
}

TEST_F(StorageManagerTest, OpenMissingDatabaseReturnsNotFound) {
    StorageManager sm;

    EXPECT_EQ(sm.OpenDatabase(path_.string()),
              StatusCode::kNotFound);
    EXPECT_FALSE(sm.IsOpen());
}

TEST_F(StorageManagerTest, CloseIsIdempotent) {
    StorageManager sm;

    EXPECT_EQ(sm.CloseDatabase(), StatusCode::kOk);

    ASSERT_EQ(sm.CreateDatabase(path_.string()),
              StatusCode::kOk);

    EXPECT_EQ(sm.CloseDatabase(), StatusCode::kOk);
    EXPECT_EQ(sm.CloseDatabase(), StatusCode::kOk);
}

TEST_F(StorageManagerTest, OperationsBeforeOpenReturnNotOpen) {
    StorageManager sm;
    std::array<char, 4096> page{};
    page_id_t page_id = kInvalidPageId;

    EXPECT_EQ(sm.AllocatePage(&page_id),
              StatusCode::kNotOpen);

    EXPECT_EQ(sm.ReadPage(0, page.data()),
              StatusCode::kNotOpen);

    EXPECT_EQ(sm.WritePage(0, page.data()),
              StatusCode::kNotOpen);

    EXPECT_EQ(sm.Flush(),
              StatusCode::kNotOpen);
}

TEST_F(StorageManagerTest, AllocatePage) {
    StorageManager sm;

    ASSERT_EQ(sm.CreateDatabase(path_.string()),
              StatusCode::kOk);

    page_id_t page_id = kInvalidPageId;

    ASSERT_EQ(sm.AllocatePage(&page_id),
              StatusCode::kOk);

    EXPECT_EQ(page_id, 1u);
    EXPECT_EQ(sm.GetPageCount(), 2u);

    ASSERT_EQ(sm.AllocatePage(&page_id),
              StatusCode::kOk);

    EXPECT_EQ(page_id, 2u);
    EXPECT_EQ(sm.GetPageCount(), 3u);

    EXPECT_EQ(sm.CloseDatabase(),
              StatusCode::kOk);
}

TEST_F(StorageManagerTest, AllocatePageInitialContentsAreZero) {
    StorageManager sm;

    ASSERT_EQ(sm.CreateDatabase(path_.string()),
              StatusCode::kOk);

    page_id_t page_id = kInvalidPageId;
    ASSERT_EQ(sm.AllocatePage(&page_id),
              StatusCode::kOk);

    std::array<char, 4096> page{};
    page.fill(static_cast<char>(0x7f));

    ASSERT_EQ(sm.ReadPage(page_id, page.data()),
              StatusCode::kOk);

    for (char value : page) {
        EXPECT_EQ(value, '\0');
    }
}

TEST_F(StorageManagerTest, ReadWriteRoundTrip) {
    StorageManager sm;

    ASSERT_EQ(sm.CreateDatabase(path_.string()),
              StatusCode::kOk);

    page_id_t page_id = kInvalidPageId;
    ASSERT_EQ(sm.AllocatePage(&page_id),
              StatusCode::kOk);

    std::array<char, 4096> write_page{};
    for (std::size_t i = 0; i < write_page.size(); ++i) {
        write_page[i] =
            static_cast<char>(i % 251);
    }

    ASSERT_EQ(sm.WritePage(page_id, write_page.data()),
              StatusCode::kOk);

    std::array<char, 4096> read_page{};
    ASSERT_EQ(sm.ReadPage(page_id, read_page.data()),
              StatusCode::kOk);

    EXPECT_EQ(read_page, write_page);
}

TEST_F(StorageManagerTest, DataPersistsAcrossCloseAndOpen) {
    {
        StorageManager sm;

        ASSERT_EQ(sm.CreateDatabase(path_.string()),
                  StatusCode::kOk);

        page_id_t page_id = kInvalidPageId;
        ASSERT_EQ(sm.AllocatePage(&page_id),
                  StatusCode::kOk);

        std::array<char, 4096> page{};
        page.fill('A');

        ASSERT_EQ(sm.WritePage(page_id, page.data()),
                  StatusCode::kOk);

        ASSERT_EQ(sm.CloseDatabase(),
                  StatusCode::kOk);
    }

    {
        StorageManager sm;

        ASSERT_EQ(sm.OpenDatabase(path_.string()),
                  StatusCode::kOk);

        EXPECT_EQ(sm.GetPageCount(), 2u);
        EXPECT_EQ(sm.GetPageSize(), 4096u);

        std::array<char, 4096> page{};

        ASSERT_EQ(sm.ReadPage(1, page.data()),
                  StatusCode::kOk);

        for (char value : page) {
            EXPECT_EQ(value, 'A');
        }

        ASSERT_EQ(sm.CloseDatabase(),
                  StatusCode::kOk);
    }
}

TEST_F(StorageManagerTest, InvalidArguments) {
    StorageManager sm;

    ASSERT_EQ(sm.CreateDatabase(path_.string()),
              StatusCode::kOk);

    std::array<char, 4096> page{};

    EXPECT_EQ(sm.AllocatePage(nullptr),
              StatusCode::kInvalidArgument);

    EXPECT_EQ(sm.ReadPage(99, page.data()),
              StatusCode::kInvalidArgument);

    EXPECT_EQ(sm.ReadPage(kInvalidPageId, page.data()),
              StatusCode::kInvalidArgument);

    EXPECT_EQ(sm.ReadPage(0, nullptr),
              StatusCode::kInvalidArgument);

    EXPECT_EQ(sm.WritePage(99, page.data()),
              StatusCode::kInvalidArgument);

    EXPECT_EQ(sm.WritePage(kInvalidPageId, page.data()),
              StatusCode::kInvalidArgument);

    EXPECT_EQ(sm.WritePage(0, nullptr),
              StatusCode::kInvalidArgument);
}

TEST_F(StorageManagerTest, FlushSucceedsWhenOpen) {
    StorageManager sm;

    ASSERT_EQ(sm.CreateDatabase(path_.string()),
              StatusCode::kOk);

    EXPECT_EQ(sm.Flush(), StatusCode::kOk);

    EXPECT_EQ(sm.CloseDatabase(),
              StatusCode::kOk);
}

TEST_F(StorageManagerTest, CorruptHeaderIsRejected) {
    {
        const int fd = ::open(
            path_.c_str(),
            O_RDWR | O_CREAT | O_TRUNC,
            0644);

        ASSERT_GE(fd, 0);

        std::array<char, 4096> corrupt{};
        corrupt[0] = 'B';
        corrupt[1] = 'A';
        corrupt[2] = 'D';

        ASSERT_EQ(::write(fd, corrupt.data(), corrupt.size()),
                  static_cast<ssize_t>(corrupt.size()));

        ASSERT_EQ(::close(fd), 0);
    }

    StorageManager sm;

    EXPECT_EQ(sm.OpenDatabase(path_.string()),
              StatusCode::kCorruption);

    EXPECT_FALSE(sm.IsOpen());
}

TEST_F(StorageManagerTest, HeaderPageCanBeRead) {
    StorageManager sm;

    ASSERT_EQ(sm.CreateDatabase(path_.string()),
              StatusCode::kOk);

    std::array<char, 4096> page{};

    ASSERT_EQ(sm.ReadPage(0, page.data()),
              StatusCode::kOk);

    EXPECT_EQ(page[0], 'T');
    EXPECT_EQ(page[1], 'I');
    EXPECT_EQ(page[2], 'N');
    EXPECT_EQ(page[3], 'Y');
    EXPECT_EQ(page[4], 'D');
    EXPECT_EQ(page[5], 'B');

    ASSERT_EQ(sm.CloseDatabase(),
              StatusCode::kOk);
}

}  // namespace
}  // namespace tinydb
