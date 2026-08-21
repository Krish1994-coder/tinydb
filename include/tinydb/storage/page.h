#pragma once
// ============================================================
// TinyDB — Page layout constants, types, PageHeader, Slot
// include/tinydb/storage/page.h
//
// Defines the physical on-disk layout of a 4 KiB data page.
//
// Page layout (byte offsets):
//
//   [0,   28)  Page Header        (kHeaderSize bytes)
//   [28,  fs)  Slot Directory     (slot_count * kSlotSize bytes)
//              free_space_start = 28 + slot_count * 6
//   [fs,  fe)  Free Space         (contiguous gap)
//   [fe, 4096) Record Data        (grows toward lower offsets)
//              free_space_end = first byte of lowest live record
//
// Growth directions:
//   Slot directory : grows downward (toward higher byte offsets)
//   Record data    : grows upward   (toward lower byte offsets)
//
// IMPORTANT — Persistent memory access:
//   PageHeader and Slot are defined as structs for static_assert
//   size/offset verification ONLY. The implementation must NOT
//   access persistent fields via reinterpret_cast over arbitrary
//   byte storage. All field access must go through memcpy-based
//   serialisation helpers.
//
// Endianness:
//   Currently uses native host endianness. Explicit rules will
//   be defined before the on-disk format is considered stable.
// ============================================================

#include "tinydb/storage/page_id.h"
#include <cstddef>
#include <cstdint>

namespace tinydb {

// ── Page size constants ────────────────────────────────────────────────────

inline constexpr std::size_t kPageSize   = 4096;
inline constexpr std::size_t kHeaderSize = 28;
inline constexpr std::size_t kSlotSize   = 6;

// Theoretical maximum slots assuming every slot has a minimum 1-byte record.
// The practical slot count is bounded by available free space.
inline constexpr std::size_t kMaxSlotsTheoretical =
    (kPageSize - kHeaderSize) / (kSlotSize + 1);

// ── PageType ──────────────────────────────────────────────────────────────

enum class PageType : uint16_t {
    kInvalid        = 0,  // Not a valid / uninitialised page
    kData           = 1,  // Heap / table data page  (Phase 2)

    // Reserved for future phases — layout not yet defined:
    kBPlusInternal  = 2,  // B+ Tree internal node   (Phase 6)
    kBPlusLeaf      = 3,  // B+ Tree leaf node        (Phase 6)
    kFreeSpace      = 4,  // Free-space directory     (Phase 5+)
    kCatalog        = 5,  // Catalog / metadata       (Phase 7+)
};

// ── PageHeader ────────────────────────────────────────────────────────────
//
// Stored at byte offset 0 in every data page.
// sizeof(PageHeader) == 28 with no implicit padding.
// Every field offset is verified by static_assert below.
//
// Field map:
//   Offset  Size  Field              Notes
//   ------  ----  -----------------  -----------------------------------
//   0       4     page_id            Logical page ID. Data pages >= 1.
//   4       2     page_type          See PageType enum.
//   6       2     slot_count         Total slots (incl. tombstones).
//   8       2     free_slot_count    Tombstone slots for reuse.
//   10      2     free_space_start   First free byte after slot dir.
//   12      2     free_space_end     First byte of lowest live record.
//   14      2     flags              Reserved. Must be 0.
//   16      4     reserved_lsn_lo   Reserved for WAL LSN (low 32 bits).
//   20      4     reserved_lsn_hi   Reserved for WAL LSN (high 32 bits).
//   24      4     checksum           Reserved. Must be 0 in Phase 2/3.
//   28      —     END
//
// NOTE on LSN: split into two uint32_t fields to avoid the 6-byte
// implicit padding that a single uint64_t at offset 18 would produce.
// When WAL is implemented (Phase 10), the combined 64-bit LSN value is:
//   lsn = (uint64_t)reserved_lsn_hi << 32 | reserved_lsn_lo
//
struct PageHeader {
    uint32_t page_id;            // offset  0, 4 bytes
    uint16_t page_type;          // offset  4, 2 bytes
    uint16_t slot_count;         // offset  6, 2 bytes
    uint16_t free_slot_count;    // offset  8, 2 bytes
    uint16_t free_space_start;   // offset 10, 2 bytes
    uint16_t free_space_end;     // offset 12, 2 bytes
    uint16_t flags;              // offset 14, 2 bytes  (reserved; must be 0)
    uint32_t reserved_lsn_lo;    // offset 16, 4 bytes  (reserved; must be 0)
    uint32_t reserved_lsn_hi;    // offset 20, 4 bytes  (reserved; must be 0)
    uint32_t checksum;           // offset 24, 4 bytes  (reserved; must be 0)
    // Total: 28 bytes, no implicit padding
};

// Size check.
static_assert(sizeof(PageHeader) == kHeaderSize,
    "PageHeader size mismatch — check field types for implicit padding.");

// Offset checks — every field must be at its documented position.
static_assert(offsetof(PageHeader, page_id)          ==  0);
static_assert(offsetof(PageHeader, page_type)        ==  4);
static_assert(offsetof(PageHeader, slot_count)       ==  6);
static_assert(offsetof(PageHeader, free_slot_count)  ==  8);
static_assert(offsetof(PageHeader, free_space_start) == 10);
static_assert(offsetof(PageHeader, free_space_end)   == 12);
static_assert(offsetof(PageHeader, flags)            == 14);
static_assert(offsetof(PageHeader, reserved_lsn_lo)  == 16);
static_assert(offsetof(PageHeader, reserved_lsn_hi)  == 20);
static_assert(offsetof(PageHeader, checksum)         == 24);

// ── Slot ──────────────────────────────────────────────────────────────────
//
// One entry in the slot directory.
// Located at: kHeaderSize + slot_id * kSlotSize
//
// Tombstone: offset == 0 && length == 0
// (Offset 0 is always the page header; no valid record can start there.)
//
struct Slot {
    uint16_t offset;    // Byte offset of record within page. 0 = tombstone.
    uint16_t length;    // Byte length of record. 0 = tombstone.
    uint16_t reserved;  // Must be 0. Future: overflow flag, MVCC bits.
};

static_assert(sizeof(Slot) == kSlotSize,
    "Slot size mismatch.");
static_assert(offsetof(Slot, offset)   == 0);
static_assert(offsetof(Slot, length)   == 2);
static_assert(offsetof(Slot, reserved) == 4);

}  // namespace tinydb
