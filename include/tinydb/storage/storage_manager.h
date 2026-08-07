#pragma once

#include <cstdint>
#include <string>

#include "tinydb/common/status.h"
#include "tinydb/storage/page_id.h"

namespace tinydb {

/// StorageManager provides the public interface to the storage engine.
///
/// It is the single point of contact between TinyDB and the underlying file
/// system. All page-level I/O in the database passes through this class.
/// Higher layers — the buffer pool, page manager, and B+ tree — must not
/// interact with the file system directly.
///
/// # Thread safety
///
/// StorageManager is NOT thread-safe. The caller is responsible for
/// synchronisation. In practice, the buffer pool manager holds a latch before
/// invoking any method here. Synchronisation primitives do not belong in this
/// class.
///
/// # Durability
///
/// WritePage is not durable on its own. Data written via WritePage may reside
/// in the OS page cache until Flush() is called. The buffer pool manager
/// decides when to flush, typically at checkpoint boundaries.
class StorageManager {
 public:
  StorageManager() = default;

  // Non-copyable, non-movable. StorageManager owns an OS file descriptor;
  // copying it would create aliased ownership with no safe release strategy.
  StorageManager(const StorageManager&)            = delete;
  StorageManager& operator=(const StorageManager&) = delete;
  StorageManager(StorageManager&&)                 = delete;
  StorageManager& operator=(StorageManager&&)      = delete;

  ~StorageManager() noexcept;

  // ---------------------------------------------------------------------------
  // Lifecycle
  // ---------------------------------------------------------------------------

  /// Creates a new database file at `path` and writes the initial file header.
  ///
  /// The file header (page 0) is initialised with the magic number, format
  /// version, configured page size, and an initial page count of 1.
  ///
  /// Returns AlreadyExists if a file already exists at `path`.
  /// On success the database is open and ready for I/O.
  auto CreateDatabase(const std::string& path) -> Status;

  /// Opens an existing database file at `path`.
  ///
  /// Reads the file header to restore page_size and page_count. Validates the
  /// magic number and format version before accepting the file.
  ///
  /// Returns NotFound if no file exists at `path`.
  /// Returns Corruption if the header cannot be validated.
  auto OpenDatabase(const std::string& path) -> Status;

  /// Closes the database file.
  ///
  /// Writes the current page count back to the file header, ensures pending
  /// writes reach stable storage, and releases the file descriptor.
  /// Safe to call more than once (idempotent).
  auto CloseDatabase() -> Status;

  // ---------------------------------------------------------------------------
  // Page allocation
  // ---------------------------------------------------------------------------

  /// Allocates a new page and returns its identifier via `out_page_id`.
  ///
  /// Extends the database file by one page (appends page_size_ zero bytes)
  /// and increments page_count_. The new identifier equals the previous
  /// page count before increment.
  ///
  /// Returns OutOfSpace if allocating another page would exceed the maximum
  /// representable page count.
  auto AllocatePage(page_id_t* out_page_id) -> Status;

  // ---------------------------------------------------------------------------
  // Page I/O
  // ---------------------------------------------------------------------------

  /// Reads exactly one page from disk into `buf`.
  ///
  /// The caller supplies a buffer sized to at least page_size_ bytes.
  /// On success the buffer is completely filled with the on-disk contents of
  /// the page identified by `page_id`.
  ///
  /// The storage engine never interprets page contents. Interpretation is the
  /// responsibility of the page manager and layers above it.
  ///
  /// Returns InvalidArgument if `page_id` is out of range or `buf` is null.
  /// Returns IOError if the underlying read operation fails or returns fewer
  /// bytes than expected (a partial page is always treated as an error).
  auto ReadPage(page_id_t page_id, char* buf) -> Status;

  /// Writes exactly one page from `buf` to disk.
  ///
  /// The caller supplies a buffer sized to at least page_size_ bytes.
  /// The on-disk page identified by `page_id` is overwritten with the full
  /// contents of the buffer.
  ///
  /// This operation is not durable on its own. Call Flush() to ensure the
  /// data reaches stable storage.
  ///
  /// Returns InvalidArgument if `page_id` is out of range or `buf` is null.
  /// Returns IOError if the underlying write operation fails or writes fewer
  /// bytes than expected.
  auto WritePage(page_id_t page_id, const char* buf) -> Status;

  // ---------------------------------------------------------------------------
  // Durability
  // ---------------------------------------------------------------------------

  /// Requests that all pending writes be pushed to stable storage.
  ///
  /// Blocks until the storage device acknowledges that all previously written
  /// data is durable. Should be called at checkpoint boundaries by the buffer
  /// pool manager — not after every individual write.
  auto Flush() -> Status;

  // ---------------------------------------------------------------------------
  // Metadata
  // ---------------------------------------------------------------------------

  /// Returns the total number of pages currently allocated in the database
  /// file, including the header page (page 0).
  auto GetPageCount() const -> uint32_t;

  /// Returns the page size in bytes.
  /// Immutable after CreateDatabase or OpenDatabase.
  auto GetPageSize() const -> uint32_t;

  /// Returns true if a database file is currently open.
  auto IsOpen() const -> bool;

 private:
  int      fd_         = -1;    // POSIX file descriptor; -1 means closed
  uint32_t page_size_  = 4096;  // bytes per page; fixed for the file's lifetime
  uint32_t page_count_ = 0;     // total allocated pages, including the header
  bool     is_open_    = false;
};

}  // namespace tinydb
