#pragma once
// ============================================================
// TinyDB — RecordID and SlotID
// include/tinydb/storage/record_id.h
//
// A RecordID uniquely identifies a record within the database.
// It consists of:
//   page_id  — which page the record lives on
//   slot_id  — which slot in that page's slot directory
//
// Stability guarantee:
//   RecordIDs are stable across:
//     - In-place record updates (same or smaller size)
//     - Record relocation within a page (larger update)
//     - Page compaction (slot IDs never change; only byte
//       offsets inside the page change)
//   RecordIDs become invalid only when:
//     - The record is deleted (DeleteRecord succeeds)
//     - The page is dropped
//
// C++17 note:
//   operator== and operator!= are explicitly implemented.
//   Defaulted comparison operators (= default) are C++20.
// ============================================================

#include "tinydb/storage/page_id.h"
#include <cstdint>
#include <limits>

namespace tinydb {

// ── SlotID ────────────────────────────────────────────────────────────────

using SlotID = uint16_t;
inline constexpr SlotID kInvalidSlotID = std::numeric_limits<uint16_t>::max();

// ── RecordID ──────────────────────────────────────────────────────────────

struct RecordID {
    page_id_t page_id{kInvalidPageId};
    SlotID    slot_id{kInvalidSlotID};

    // Returns true if neither field holds a sentinel invalid value.
    bool IsValid() const noexcept {
        return page_id != kInvalidPageId &&
               slot_id != kInvalidSlotID;
    }

    // C++17: operator== must be explicitly implemented.
    // Defaulted comparison (= default) is a C++20 feature.
    bool operator==(const RecordID& other) const noexcept {
        return page_id == other.page_id &&
               slot_id == other.slot_id;
    }

    bool operator!=(const RecordID& other) const noexcept {
        return !(*this == other);
    }
};

inline constexpr RecordID kInvalidRecordID{kInvalidPageId, kInvalidSlotID};

}  // namespace tinydb
