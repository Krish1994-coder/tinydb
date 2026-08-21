#pragma once
// ============================================================
// TinyDB — PageManager public interface
// include/tinydb/storage/page_manager.h
//
// PageManager defines and implements the page layout semantics
// and record manipulation algorithms for TinyDB data pages.
//
// It operates purely on in-memory page buffers supplied by the
// caller (typically the Buffer Pool). It performs NO disk I/O
// and does NOT own the page memory.
//
// ── Memory contract ──────────────────────────────────────────
//   - 'page' points to exactly kPageSize bytes.
//   - 'page' is writable for all mutating operations.
//   - 'page' is owned by the Buffer Pool; PageManager never
//     allocates or frees page memory.
//   - PageManager never stores a pointer to 'page' between calls.
//
// ── Persistent memory access ─────────────────────────────────
//   All persistent fields are accessed through memcpy-based
//   helpers. reinterpret_cast over the raw byte buffer is NOT
//   used; the struct definitions exist for static_assert only.
//
// ── ByteView lifetime ────────────────────────────────────────
//   A ByteView from GetRecord() is valid only while:
//     (1) the page remains pinned in the Buffer Pool, AND
//     (2) no mutation (DeleteRecord, UpdateRecord relocation,
//         or Compact) invalidates the referenced record bytes.
//
// ── Aliasing precondition ────────────────────────────────────
//   For InsertRecord and UpdateRecord: input data must NOT
//   overlap the destination page memory. Copy data out of the
//   page before passing it as input.
//
// ── Component boundaries ─────────────────────────────────────
//   StorageManager — persistent file I/O (ReadPage / WritePage)
//   BufferPool     — frame memory, pin counts, dirty state
//   PageManager    — page layout + record manipulation (here)
//   Catalog layer  — schema, types, NULL encoding
// ============================================================

#include "tinydb/storage/page.h"
#include "tinydb/storage/record_id.h"
#include "tinydb/common/byte_view.h"
#include "tinydb/common/result.h"
#include <cstddef>
#include <cstdint>

namespace tinydb {

class PageManager {
public:
    PageManager()                              = default;
    ~PageManager()                             = default;

    PageManager(const PageManager&)            = delete;
    PageManager& operator=(const PageManager&) = delete;
    PageManager(PageManager&&)                 = default;
    PageManager& operator=(PageManager&&)      = default;

    // ── Initialisation ──────────────────────────────────────────────────────

    // Initialise a brand-new, empty data page.
    // page_id must be >= 1 (page 0 is the file header).
    // Returns Status::InvalidArgument if page_id == kInvalidPageId.
    Status Init(std::byte* page, page_id_t page_id) noexcept;

    // Load an existing page already read into memory.
    // Runs ValidatePage internally.
    // Returns Status::Corruption if any invariant is violated.
    Status InitFromExistingPage(std::byte* page) noexcept;

    // ── Record Operations ───────────────────────────────────────────────────

    // Insert a variable-length record. Returns the stable RecordID.
    // Precondition: [data, data+size) must not overlap page memory.
    // Errors: InvalidArgument (size==0 or too large), PageFull.
    Result<RecordID> InsertRecord(std::byte*       page,
                                  const std::byte* data,
                                  std::size_t      size) noexcept;

    // Retrieve a record by RecordID. Returns a non-owning ByteView.
    // Errors: InvalidRecordID, RecordNotFound, Corruption.
    Result<ByteView> GetRecord(const std::byte* page,
                               RecordID         rid) const noexcept;

    // Mark a record as deleted (tombstone). NOT idempotent.
    // Errors: InvalidRecordID, RecordNotFound, Corruption.
    Status DeleteRecord(std::byte* page, RecordID rid) noexcept;

    // Update a record in-place (<=) or by relocation (>).
    // Preserves RecordID (page_id + slot_id) in both cases.
    // Precondition: [new_data, new_data+new_size) must not overlap page.
    // Errors: InvalidRecordID, RecordNotFound, PageFull,
    //         InvalidArgument, Corruption.
    Result<RecordID> UpdateRecord(std::byte*       page,
                                  RecordID         rid,
                                  const std::byte* new_data,
                                  std::size_t      new_size) noexcept;

    // ── Space and Layout Queries ────────────────────────────────────────────

    // Contiguous free bytes available without compaction.
    uint16_t GetFreeSpace(const std::byte* page) const noexcept;

    // Total slot count (including tombstones).
    uint16_t GetSlotCount(const std::byte* page) const noexcept;

    // Count of tombstone slots available for reuse.
    uint16_t GetFreeSlotCount(const std::byte* page) const noexcept;

    // ── Compaction ──────────────────────────────────────────────────────────

    // Pack all live records to reclaim fragmented space.
    // Slot IDs and RecordIDs are preserved.
    // All ByteView instances into this page are INVALID after this returns.
    // 'scratch' must point to a caller-provided kPageSize-byte buffer.
    Status Compact(std::byte* page, std::byte* scratch) noexcept;

    // ── Validation ──────────────────────────────────────────────────────────

    // Run all page invariants (I-01 through I-18).
    // Returns Corruption on first violation.
    Status ValidatePage(const std::byte* page) const noexcept;

private:
    // ── Private serialisation helpers ───────────────────────────────────────
    // All page field access uses memcpy — never reinterpret_cast.
    // Defined in page_manager.cpp.

    void ReadHeader(const std::byte* page, PageHeader& out) const noexcept;
    void WriteHeader(std::byte* page, const PageHeader& hdr) noexcept;
    void ReadSlot(const std::byte* page, SlotID slot_id, Slot& out) const noexcept;
    void WriteSlot(std::byte* page, SlotID slot_id, const Slot& slot) noexcept;

    bool IsValidSlotID(const PageHeader& hdr, SlotID id) const noexcept;
    bool IsTombstone(const Slot& slot)                   const noexcept;
};

}  // namespace tinydb
