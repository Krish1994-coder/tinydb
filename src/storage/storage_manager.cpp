#include "tinydb/storage/storage_manager.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <string>

namespace tinydb {

namespace {

constexpr uint32_t kDefaultPageSize = 4096;

// File header constants.
// Page 0 is reserved for the database file header.
constexpr std::size_t kMagicOffset = 0;
constexpr std::size_t kVersionOffset = 8;
constexpr std::size_t kPageSizeOffset = 12;
constexpr std::size_t kPageCountOffset = 16;

constexpr std::size_t kMagicSize = 8;
constexpr std::size_t kFileHeaderSize = 20;

constexpr std::array<char, kMagicSize> kMagic = {
    'T', 'I', 'N', 'Y', 'D', 'B', '\0', '\0'
};

constexpr uint32_t kFormatVersion = 1;

void StoreU32(char* buf, std::size_t offset, uint32_t value) {
    std::memcpy(buf + offset, &value, sizeof(value));
}

uint32_t LoadU32(const char* buf, std::size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, buf + offset, sizeof(value));
    return value;
}

Status ErrnoStatus(const char* operation) {
    const int saved_errno = errno;

    return Status::IOError(
        std::string(operation) + " failed: errno=" +
        std::to_string(saved_errno) + " (" +
        std::strerror(saved_errno) + ")");
}

bool ReadFully(int fd, char* buf, std::size_t size) {
    std::size_t total = 0;

    while (total < size) {
        const ssize_t n = ::read(fd, buf + total, size - total);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (n == 0) {
            return false;
        }

        total += static_cast<std::size_t>(n);
    }

    return true;
}

bool WriteFully(int fd, const char* buf, std::size_t size) {
    std::size_t total = 0;

    while (total < size) {
        const ssize_t n = ::write(fd, buf + total, size - total);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (n == 0) {
            return false;
        }

        total += static_cast<std::size_t>(n);
    }

    return true;
}

Status ValidatePath(const std::string& path) {
    if (path.empty()) {
        return Status::InvalidArgument("database path must not be empty");
    }

    return Status::Ok();
}

Status ValidatePageID(page_id_t page_id, uint32_t page_count) {
    if (page_id == kInvalidPageId) {
        return Status::InvalidArgument("page_id is invalid");
    }

    if (page_id >= page_count) {
        return Status::InvalidArgument("page_id is out of range");
    }

    return Status::Ok();
}

bool CalculateOffset(page_id_t page_id,
                     uint32_t page_size,
                     off_t* out_offset) {
    if (out_offset == nullptr) {
        return false;
    }

    const uint64_t offset =
        static_cast<uint64_t>(page_id) *
        static_cast<uint64_t>(page_size);

    if (offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
        return false;
    }

    *out_offset = static_cast<off_t>(offset);
    return true;
}

Status WriteFileHeader(int fd, uint32_t page_size, uint32_t page_count) {
    std::array<char, kDefaultPageSize> page{};

    std::memcpy(page.data() + kMagicOffset,
                kMagic.data(),
                kMagic.size());

    StoreU32(page.data(), kVersionOffset, kFormatVersion);
    StoreU32(page.data(), kPageSizeOffset, page_size);
    StoreU32(page.data(), kPageCountOffset, page_count);

    if (::lseek(fd, 0, SEEK_SET) < 0) {
        return ErrnoStatus("lseek");
    }

    if (!WriteFully(fd, page.data(), page.size())) {
        return ErrnoStatus("write");
    }

    return Status::Ok();
}

Status ReadAndValidateFileHeader(int fd,
                                 uint32_t* out_page_size,
                                 uint32_t* out_page_count) {
    if (out_page_size == nullptr || out_page_count == nullptr) {
        return Status::InvalidArgument("output pointer must not be null");
    }

    std::array<char, kDefaultPageSize> page{};

    if (::lseek(fd, 0, SEEK_SET) < 0) {
        return ErrnoStatus("lseek");
    }

    if (!ReadFully(fd, page.data(), page.size())) {
        return Status::Corruption("file header is incomplete");
    }

    if (std::memcmp(page.data() + kMagicOffset,
                    kMagic.data(),
                    kMagic.size()) != 0) {
        return Status::Corruption("invalid database magic number");
    }

    const uint32_t version = LoadU32(page.data(), kVersionOffset);
    if (version != kFormatVersion) {
        return Status::Corruption("unsupported database format version");
    }

    const uint32_t page_size = LoadU32(page.data(), kPageSizeOffset);
    const uint32_t page_count = LoadU32(page.data(), kPageCountOffset);

    if (page_size == 0) {
        return Status::Corruption("invalid page size");
    }

    if (page_count == 0) {
        return Status::Corruption("invalid page count");
    }

    if (page_size != kDefaultPageSize) {
        return Status::Corruption("unsupported page size");
    }

    *out_page_size = page_size;
    *out_page_count = page_count;

    return Status::Ok();
}

}  // namespace

StorageManager::~StorageManager() noexcept {
    if (is_open_) {
        if (fd_ >= 0) {
            ::fsync(fd_);
            ::close(fd_);
        }

        fd_ = -1;
        is_open_ = false;
        page_count_ = 0;
        page_size_ = kDefaultPageSize;
    }
}

auto StorageManager::CreateDatabase(const std::string& path) -> Status {
    if (auto status = ValidatePath(path); !status.IsOk()) {
        return status;
    }

    if (is_open_) {
        return Status::InvalidArgument("a database is already open");
    }

    const int fd = ::open(
        path.c_str(),
        O_RDWR | O_CREAT | O_EXCL,
        0644);

    if (fd < 0) {
        if (errno == EEXIST) {
            return Status::AlreadyExists(
                "database file already exists");
        }

        return ErrnoStatus("open");
    }

    const Status header_status =
        WriteFileHeader(fd, kDefaultPageSize, 1);

    if (!header_status.IsOk()) {
        ::close(fd);
        ::unlink(path.c_str());
        return header_status;
    }

    if (::fsync(fd) < 0) {
        const Status status = ErrnoStatus("fsync");
        ::close(fd);
        ::unlink(path.c_str());
        return status;
    }

    fd_ = fd;
    page_size_ = kDefaultPageSize;
    page_count_ = 1;
    is_open_ = true;

    return Status::Ok();
}

auto StorageManager::OpenDatabase(const std::string& path) -> Status {
    if (auto status = ValidatePath(path); !status.IsOk()) {
        return status;
    }

    if (is_open_) {
        return Status::InvalidArgument("a database is already open");
    }

    const int fd = ::open(path.c_str(), O_RDWR);

    if (fd < 0) {
        if (errno == ENOENT) {
            return Status::NotFound(
                "database file does not exist");
        }

        return ErrnoStatus("open");
    }

    uint32_t page_size = 0;
    uint32_t page_count = 0;

    const Status header_status =
        ReadAndValidateFileHeader(
            fd, &page_size, &page_count);

    if (!header_status.IsOk()) {
        ::close(fd);
        return header_status;
    }

    fd_ = fd;
    page_size_ = page_size;
    page_count_ = page_count;
    is_open_ = true;

    return Status::Ok();
}

auto StorageManager::CloseDatabase() -> Status {
    if (!is_open_) {
        return Status::Ok();
    }

    const Status flush_status = Flush();
    if (!flush_status.IsOk()) {
        return flush_status;
    }

    const Status header_status =
        WriteFileHeader(fd_, page_size_, page_count_);

    if (!header_status.IsOk()) {
        return header_status;
    }

    if (::fsync(fd_) < 0) {
        return ErrnoStatus("fsync");
    }

    if (::close(fd_) < 0) {
        return ErrnoStatus("close");
    }

    fd_ = -1;
    is_open_ = false;
    page_count_ = 0;
    page_size_ = kDefaultPageSize;

    return Status::Ok();
}

auto StorageManager::AllocatePage(page_id_t* out_page_id) -> Status {
    if (!is_open_) {
        return Status::NotOpen("database is not open");
    }

    if (out_page_id == nullptr) {
        return Status::InvalidArgument(
            "out_page_id must not be null");
    }

    if (page_count_ == std::numeric_limits<page_id_t>::max()) {
        return Status::OutOfSpace(
            "maximum page count reached");
    }

    const page_id_t new_page_id = page_count_;

    off_t offset = 0;
    if (!CalculateOffset(new_page_id, page_size_, &offset)) {
        return Status::OutOfSpace(
            "page offset is not representable");
    }

    if (::lseek(fd_, offset, SEEK_SET) < 0) {
        return ErrnoStatus("lseek");
    }

    std::array<char, kDefaultPageSize> zero_page{};

    if (!WriteFully(fd_, zero_page.data(), page_size_)) {
        return ErrnoStatus("write");
    }

    ++page_count_;
    *out_page_id = new_page_id;

    return Status::Ok();
}

auto StorageManager::ReadPage(page_id_t page_id, char* buf) -> Status {
    if (!is_open_) {
        return Status::NotOpen("database is not open");
    }

    if (buf == nullptr) {
        return Status::InvalidArgument("buffer must not be null");
    }

    const Status id_status =
        ValidatePageID(page_id, page_count_);

    if (!id_status.IsOk()) {
        return id_status;
    }

    off_t offset = 0;
    if (!CalculateOffset(page_id, page_size_, &offset)) {
        return Status::InvalidArgument(
            "page offset is not representable");
    }

    if (::lseek(fd_, offset, SEEK_SET) < 0) {
        return ErrnoStatus("lseek");
    }

    std::size_t total = 0;

    while (total < page_size_) {
        const ssize_t n =
            ::read(fd_, buf + total, page_size_ - total);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            return ErrnoStatus("read");
        }

        if (n == 0) {
            return Status::IOError(
                "short read while reading page");
        }

        total += static_cast<std::size_t>(n);
    }

    return Status::Ok();
}

auto StorageManager::WritePage(page_id_t page_id,
                                const char* buf) -> Status {
    if (!is_open_) {
        return Status::NotOpen("database is not open");
    }

    if (buf == nullptr) {
        return Status::InvalidArgument("buffer must not be null");
    }

    const Status id_status =
        ValidatePageID(page_id, page_count_);

    if (!id_status.IsOk()) {
        return id_status;
    }

    off_t offset = 0;
    if (!CalculateOffset(page_id, page_size_, &offset)) {
        return Status::InvalidArgument(
            "page offset is not representable");
    }

    if (::lseek(fd_, offset, SEEK_SET) < 0) {
        return ErrnoStatus("lseek");
    }

    if (!WriteFully(fd_, buf, page_size_)) {
        return ErrnoStatus("write");
    }

    return Status::Ok();
}

auto StorageManager::Flush() -> Status {
    if (!is_open_) {
        return Status::NotOpen("database is not open");
    }

    if (::fsync(fd_) < 0) {
        return ErrnoStatus("fsync");
    }

    return Status::Ok();
}

auto StorageManager::GetPageCount() const -> uint32_t {
    return page_count_;
}

auto StorageManager::GetPageSize() const -> uint32_t {
    return page_size_;
}

auto StorageManager::IsOpen() const -> bool {
    return is_open_;
}

}  // namespace tinydb
