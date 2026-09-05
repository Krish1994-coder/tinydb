// ============================================================
// TinyDB — PageManager implementation
// src/storage/page_manager.cpp
//
// Specification: docs/02_page_manager.md
//
// Design rules:
//  - All persistent page fields accessed via memcpy helpers only.
//  - No reinterpret_cast over raw page storage.
//  - Aliasing handled by copying source into a local buffer before
//    any page mutation (Insert, Update — both in-place and relocation).
//  - Every mutating operation validates the full page first and leaves
//    the page byte-for-byte unchanged on any error return.
//  - UpdateRecord atomicity: compaction runs on a scratch copy;
//    the original page is committed only after the full update succeeds.
// ============================================================

#include "tinydb/storage/page_manager.h"

#include <cstdint>
#include <cstring>

namespace tinydb {

// ── Serialisation field offsets ──────────────────────────────────────────────
// Must match the documented PageHeader layout verified in page.h static_asserts.

static constexpr std::size_t kOff_page_id          =  0;
static constexpr std::size_t kOff_page_type        =  4;
static constexpr std::size_t kOff_slot_count       =  6;
static constexpr std::size_t kOff_free_slot_count  =  8;
static constexpr std::size_t kOff_free_space_start = 10;
static constexpr std::size_t kOff_free_space_end   = 12;
static constexpr std::size_t kOff_flags            = 14;
static constexpr std::size_t kOff_reserved_lsn_lo  = 16;
static constexpr std::size_t kOff_reserved_lsn_hi  = 20;
static constexpr std::size_t kOff_checksum         = 24;

static constexpr std::size_t kSlotOff_offset   = 0;
static constexpr std::size_t kSlotOff_length   = 2;
static constexpr std::size_t kSlotOff_reserved = 4;

// Maximum number of slots that physically fit in a page.
static constexpr uint16_t kMaxSlots =
    static_cast<uint16_t>((kPageSize - kHeaderSize) / kSlotSize);

// ── memcpy helpers ───────────────────────────────────────────────────────────

template <typename T>
static void ReadField(const std::byte* base, std::size_t off, T& out) noexcept {
    std::memcpy(&out, base + off, sizeof(T));
}

template <typename T>
static void WriteField(std::byte* base, std::size_t off, const T& val) noexcept {
    std::memcpy(base + off, &val, sizeof(T));
}

// ── Address / range helpers ──────────────────────────────────────────────────
// We use std::uintptr_t for address arithmetic so that comparisons between
// pointers belonging to different C++ objects are well-defined at the
// implementation level (common on all supported platforms).
// Overflow is prevented by checking sizes before adding them.

// Returns true if the half-open range [data, data+size) overlaps with the
// kPageSize-byte region starting at page_base.
// Precondition: size > 0 and size <= kPageSize (both satisfied by callers).
static bool RangeOverlapsPage(const std::byte* page_base,
                              const std::byte* data,
                              std::size_t      size) noexcept {
    // Use uintptr_t for address arithmetic — avoids pointer provenance issues.
    const auto ip  = reinterpret_cast<std::uintptr_t>(page_base);  // NOLINT
    const auto id  = reinterpret_cast<std::uintptr_t>(data);        // NOLINT

    // ip + kPageSize: kPageSize <= 65536, ip is a valid address — no wrap on
    // any realistic platform.  Same for id + size (size <= kPageSize).
    const auto ip_end = ip + static_cast<std::uintptr_t>(kPageSize);
    const auto id_end = id + static_cast<std::uintptr_t>(size);

    // Ranges overlap iff neither ends at or before the other starts.
    return !(id_end <= ip || ip_end <= id);
}

// Returns true if the two kPageSize-byte regions starting at a and b overlap.
static bool RegionsOverlap(const std::byte* a, const std::byte* b) noexcept {
    const auto ia     = reinterpret_cast<std::uintptr_t>(a);  // NOLINT
    const auto ib     = reinterpret_cast<std::uintptr_t>(b);  // NOLINT
    const auto ia_end = ia + static_cast<std::uintptr_t>(kPageSize);
    const auto ib_end = ib + static_cast<std::uintptr_t>(kPageSize);
    return !(ia_end <= ib || ib_end <= ia);
}

// ── Private serialisation helpers ────────────────────────────────────────────

void PageManager::ReadHeader(const std::byte* page, PageHeader& h) const noexcept {
    ReadField(page, kOff_page_id,          h.page_id);
    ReadField(page, kOff_page_type,        h.page_type);
    ReadField(page, kOff_slot_count,       h.slot_count);
    ReadField(page, kOff_free_slot_count,  h.free_slot_count);
    ReadField(page, kOff_free_space_start, h.free_space_start);
    ReadField(page, kOff_free_space_end,   h.free_space_end);
    ReadField(page, kOff_flags,            h.flags);
    ReadField(page, kOff_reserved_lsn_lo,  h.reserved_lsn_lo);
    ReadField(page, kOff_reserved_lsn_hi,  h.reserved_lsn_hi);
    ReadField(page, kOff_checksum,         h.checksum);
}

void PageManager::WriteHeader(std::byte* page, const PageHeader& h) noexcept {
    WriteField(page, kOff_page_id,          h.page_id);
    WriteField(page, kOff_page_type,        h.page_type);
    WriteField(page, kOff_slot_count,       h.slot_count);
    WriteField(page, kOff_free_slot_count,  h.free_slot_count);
    WriteField(page, kOff_free_space_start, h.free_space_start);
    WriteField(page, kOff_free_space_end,   h.free_space_end);
    WriteField(page, kOff_flags,            h.flags);
    WriteField(page, kOff_reserved_lsn_lo,  h.reserved_lsn_lo);
    WriteField(page, kOff_reserved_lsn_hi,  h.reserved_lsn_hi);
    WriteField(page, kOff_checksum,         h.checksum);
}

void PageManager::ReadSlot(const std::byte* page, SlotID id, Slot& s) const noexcept {
    const std::size_t base = kHeaderSize + static_cast<std::size_t>(id) * kSlotSize;
    ReadField(page, base + kSlotOff_offset,   s.offset);
    ReadField(page, base + kSlotOff_length,   s.length);
    ReadField(page, base + kSlotOff_reserved, s.reserved);
}

void PageManager::WriteSlot(std::byte* page, SlotID id, const Slot& s) noexcept {
    const std::size_t base = kHeaderSize + static_cast<std::size_t>(id) * kSlotSize;
    WriteField(page, base + kSlotOff_offset,   s.offset);
    WriteField(page, base + kSlotOff_length,   s.length);
    WriteField(page, base + kSlotOff_reserved, s.reserved);
}

bool PageManager::IsValidSlotID(const PageHeader& hdr, SlotID id) const noexcept {
    return id < hdr.slot_count;
}

bool PageManager::IsTombstone(const Slot& s) const noexcept {
    return s.offset == 0 && s.length == 0;
}

// Validates a live (non-tombstone) slot's internal fields.
// Does NOT check I-11 (offset vs free_space_end) because that requires the
// header and is enforced by ValidatePage; here we only check the slot fields.
static Status ValidateLiveSlot(const Slot& s) noexcept {
    if (s.length == 0) {
        return Status::Corruption("live slot has zero length (not a tombstone)");
    }
    if (s.reserved != 0) {
        return Status::Corruption("slot reserved field != 0");
    }
    if (s.offset < static_cast<uint16_t>(kHeaderSize)) {
        return Status::Corruption("slot offset < kHeaderSize");
    }
    const uint32_t slot_end = static_cast<uint32_t>(s.offset) +
                              static_cast<uint32_t>(s.length);
    if (slot_end > static_cast<uint32_t>(kPageSize)) {
        return Status::Corruption("slot offset+length > kPageSize");
    }
    return Status::Ok();
}

// ── Init ─────────────────────────────────────────────────────────────────────

Status PageManager::Init(std::byte* page, page_id_t page_id) noexcept {
    if (page == nullptr) {
        return Status::InvalidArgument("page must not be null");
    }
    if (page_id == kInvalidPageId || page_id == 0) {
        return Status::InvalidArgument("page_id must be >= 1");
    }
    std::memset(page, 0, kPageSize);
    PageHeader h{};
    h.page_id          = page_id;
    h.page_type        = static_cast<uint16_t>(PageType::kData);
    h.slot_count       = 0;
    h.free_slot_count  = 0;
    h.free_space_start = static_cast<uint16_t>(kHeaderSize);
    h.free_space_end   = static_cast<uint16_t>(kPageSize);
    h.flags            = 0;
    h.reserved_lsn_lo  = 0;
    h.reserved_lsn_hi  = 0;
    h.checksum         = 0;
    WriteHeader(page, h);
    return Status::Ok();
}

// ── InitFromExistingPage ──────────────────────────────────────────────────────

Status PageManager::InitFromExistingPage(std::byte* page) noexcept {
    if (page == nullptr) {
        return Status::InvalidArgument("page must not be null");
    }
    return ValidatePage(page);
}

// ── ValidatePage ──────────────────────────────────────────────────────────────

Status PageManager::ValidatePage(const std::byte* page) const noexcept {
    if (page == nullptr) {
        return Status::InvalidArgument("page must not be null");
    }

    PageHeader h{};
    ReadHeader(page, h);

    // I-01
    if (h.page_id < 1) {
        return Status::Corruption("I-01: page_id < 1");
    }
    // I-02
    if (h.page_type != static_cast<uint16_t>(PageType::kData)) {
        return Status::Corruption("I-02: page_type is not kData");
    }
    // I-03 (overflow-safe: kMaxSlots is a compile-time constant)
    if (h.slot_count > kMaxSlots) {
        return Status::Corruption("I-03: slot_count too large");
    }
    // I-04
    if (h.free_slot_count > h.slot_count) {
        return Status::Corruption("I-04: free_slot_count > slot_count");
    }
    // I-05 (overflow-safe via uint32_t)
    {
        const uint32_t expected = static_cast<uint32_t>(kHeaderSize) +
                                  static_cast<uint32_t>(h.slot_count) * kSlotSize;
        if (static_cast<uint32_t>(h.free_space_start) != expected) {
            return Status::Corruption("I-05: free_space_start inconsistent");
        }
    }
    // I-06
    if (h.free_space_end < h.free_space_start) {
        return Status::Corruption("I-06: free_space_end < free_space_start");
    }
    // I-07
    if (static_cast<uint32_t>(h.free_space_end) > static_cast<uint32_t>(kPageSize)) {
        return Status::Corruption("I-07: free_space_end > kPageSize");
    }
    // I-08
    if (h.checksum != 0) {
        return Status::Corruption("I-08: checksum != 0");
    }
    // I-09
    if (h.reserved_lsn_lo != 0 || h.reserved_lsn_hi != 0) {
        return Status::Corruption("I-09: reserved_lsn non-zero");
    }
    // I-10
    if (h.flags != 0) {
        return Status::Corruption("I-10: flags != 0");
    }

    uint16_t tombstone_count = 0;

    for (uint16_t i = 0; i < h.slot_count; ++i) {
        Slot s{};
        ReadSlot(page, i, s);

        if (IsTombstone(s)) {
            // I-15
            if (s.reserved != 0) {
                return Status::Corruption("I-15: tombstone reserved != 0");
            }
            ++tombstone_count;
            continue;
        }

        // Malformed: offset != 0 but length == 0 — not a tombstone, not valid.
        if (s.length == 0) {
            return Status::Corruption("I-13: live slot has zero length");
        }
        // I-14
        if (s.reserved != 0) {
            return Status::Corruption("I-14: live slot reserved != 0");
        }
        // I-12a: offset >= kHeaderSize
        if (s.offset < static_cast<uint16_t>(kHeaderSize)) {
            return Status::Corruption("I-12: slot offset < kHeaderSize");
        }
        // I-12b: overflow-safe end check
        {
            const uint32_t slot_end = static_cast<uint32_t>(s.offset) +
                                      static_cast<uint32_t>(s.length);
            if (slot_end > static_cast<uint32_t>(kPageSize)) {
                return Status::Corruption("I-12: slot offset+length > kPageSize");
            }
        }
        // I-11: record lies in record-data region (below free_space_end)
        if (static_cast<uint32_t>(s.offset) < static_cast<uint32_t>(h.free_space_end)) {
            return Status::Corruption("I-11: live slot offset < free_space_end");
        }
    }

    // I-17
    if (tombstone_count != h.free_slot_count) {
        return Status::Corruption("I-17: tombstone count != free_slot_count");
    }

    // I-16: no two live records overlap (O(n^2), acceptable for kPageSize pages)
    for (uint16_t i = 0; i < h.slot_count; ++i) {
        Slot si{};
        ReadSlot(page, i, si);
        if (IsTombstone(si) || si.length == 0) continue;

        for (uint16_t j = static_cast<uint16_t>(i + 1); j < h.slot_count; ++j) {
            Slot sj{};
            ReadSlot(page, j, sj);
            if (IsTombstone(sj) || sj.length == 0) continue;

            const uint32_t si_end = static_cast<uint32_t>(si.offset) + si.length;
            const uint32_t sj_end = static_cast<uint32_t>(sj.offset) + sj.length;
            if (si.offset < sj_end && sj.offset < si_end) {
                return Status::Corruption("I-16: overlapping live slot ranges");
            }
        }
    }
    // I-18 is implied by I-05 + I-06.

    return Status::Ok();
}

// ── GetFreeSpace / GetSlotCount / GetFreeSlotCount ───────────────────────────

uint16_t PageManager::GetFreeSpace(const std::byte* page) const noexcept {
    PageHeader h{};
    ReadHeader(page, h);
    if (h.free_space_end < h.free_space_start) return 0;
    return static_cast<uint16_t>(h.free_space_end - h.free_space_start);
}

uint16_t PageManager::GetSlotCount(const std::byte* page) const noexcept {
    uint16_t v{};
    ReadField(page, kOff_slot_count, v);
    return v;
}

uint16_t PageManager::GetFreeSlotCount(const std::byte* page) const noexcept {
    uint16_t v{};
    ReadField(page, kOff_free_slot_count, v);
    return v;
}

// ── InsertRecord ──────────────────────────────────────────────────────────────
//
// Aliasing policy:
//   InsertRecord accepts data that may point into the page.
//   If [data, data+size) overlaps [page, page+kPageSize), we copy the
//   source bytes into a local buffer before modifying free_space_end so
//   the source is always valid when the final memcpy executes.

Result<RecordID> PageManager::InsertRecord(std::byte*       page,
                                           const std::byte* data,
                                           std::size_t      size) noexcept {
    if (page == nullptr || data == nullptr) {
        return Result<RecordID>::Err(
            Status::InvalidArgument("page and data must not be null"));
    }
    if (size == 0) {
        return Result<RecordID>::Err(
            Status::InvalidArgument("record size must be > 0"));
    }

    // Reject globally corrupt pages before any mutation.
    {
        Status v = ValidatePage(page);
        if (!v.IsOk()) return Result<RecordID>::Err(v);
    }

    PageHeader h{};
    ReadHeader(page, h);

    // Find first tombstone for reuse.
    SlotID reuse_id = kInvalidSlotID;
    for (uint16_t i = 0; i < h.slot_count; ++i) {
        Slot s{};
        ReadSlot(page, i, s);
        if (IsTombstone(s)) { reuse_id = i; break; }
    }
    const bool reuse = (reuse_id != kInvalidSlotID);

    // Slot-aware maximum record size.
    constexpr std::size_t kMaxReuse   = kPageSize - kHeaderSize;            // 4068
    constexpr std::size_t kMaxNewSlot = kPageSize - kHeaderSize - kSlotSize;// 4062
    const std::size_t max_size = reuse ? kMaxReuse : kMaxNewSlot;
    if (size > max_size) {
        return Result<RecordID>::Err(
            Status::InvalidArgument("record size exceeds page capacity"));
    }

    // Contiguous free space check (overflow-safe via uint32_t).
    const uint32_t needed32 = reuse
        ? static_cast<uint32_t>(size)
        : static_cast<uint32_t>(size) + static_cast<uint32_t>(kSlotSize);
    if (needed32 > static_cast<uint32_t>(GetFreeSpace(page))) {
        return Result<RecordID>::Err(Status::PageFull("insufficient free space"));
    }

    // ── Aliasing: copy source into local buffer if it overlaps the page ──────
    // We do this BEFORE modifying free_space_end so the source bytes remain
    // valid regardless of where in the page they live.
    std::byte src_buf[kPageSize];
    const std::byte* src = data;
    if (RangeOverlapsPage(page, data, size)) {
        std::memcpy(src_buf, data, size);
        src = src_buf;
    }

    // Place the record at the new free_space_end.
    h.free_space_end -= static_cast<uint16_t>(size);
    std::memcpy(page + h.free_space_end, src, size);

    Slot new_slot{};
    new_slot.offset   = h.free_space_end;
    new_slot.length   = static_cast<uint16_t>(size);
    new_slot.reserved = 0;

    SlotID slot_id{};
    if (reuse) {
        slot_id = reuse_id;
        --h.free_slot_count;
    } else {
        slot_id = h.slot_count;
        ++h.slot_count;
        h.free_space_start = static_cast<uint16_t>(
            static_cast<uint32_t>(kHeaderSize) +
            static_cast<uint32_t>(h.slot_count) * kSlotSize);
    }

    WriteSlot(page, slot_id, new_slot);
    WriteHeader(page, h);

    return Result<RecordID>::Ok(RecordID{h.page_id, slot_id});
}

// ── GetRecord ─────────────────────────────────────────────────────────────────

Result<ByteView> PageManager::GetRecord(const std::byte* page,
                                        RecordID         rid) const noexcept {
    if (page == nullptr) {
        return Result<ByteView>::Err(Status::InvalidArgument("page must not be null"));
    }

    // Reject globally corrupt pages.
    {
        Status v = ValidatePage(page);
        if (!v.IsOk()) return Result<ByteView>::Err(v);
    }

    PageHeader h{};
    ReadHeader(page, h);

    if (rid.page_id != h.page_id) {
        return Result<ByteView>::Err(Status::InvalidRecordID("page_id mismatch"));
    }
    if (!IsValidSlotID(h, rid.slot_id)) {
        return Result<ByteView>::Err(Status::InvalidRecordID("slot_id out of range"));
    }

    Slot s{};
    ReadSlot(page, rid.slot_id, s);

    if (IsTombstone(s)) {
        return Result<ByteView>::Err(Status::RecordNotFound("slot is a tombstone"));
    }

    {
        Status vs = ValidateLiveSlot(s);
        if (!vs.IsOk()) return Result<ByteView>::Err(vs);
    }

    ByteView view{page + s.offset, s.length};
    return Result<ByteView>::Ok(view);
}

// ── DeleteRecord ──────────────────────────────────────────────────────────────

Status PageManager::DeleteRecord(std::byte* page, RecordID rid) noexcept {
    if (page == nullptr) {
        return Status::InvalidArgument("page must not be null");
    }

    // Reject globally corrupt pages before any mutation.
    {
        Status v = ValidatePage(page);
        if (!v.IsOk()) return v;
    }

    PageHeader h{};
    ReadHeader(page, h);

    if (rid.page_id != h.page_id) {
        return Status::InvalidRecordID("page_id mismatch");
    }
    if (!IsValidSlotID(h, rid.slot_id)) {
        return Status::InvalidRecordID("slot_id out of range");
    }

    Slot s{};
    ReadSlot(page, rid.slot_id, s);

    if (IsTombstone(s)) {
        return Status::RecordNotFound("slot is already a tombstone");
    }

    {
        Status vs = ValidateLiveSlot(s);
        if (!vs.IsOk()) return vs;
    }

    // All validation passed — write tombstone then update header.
    Slot tombstone{};
    WriteSlot(page, rid.slot_id, tombstone);

    ++h.free_slot_count;
    WriteHeader(page, h);

    return Status::Ok();
}

// ── UpdateRecord ──────────────────────────────────────────────────────────────
//
// Atomicity contract (from docs/02_page_manager.md):
//   A failed UpdateRecord must not leave the page in a partial state.
//
// Implementation strategy:
//   All work is done on a scratch copy of the page.
//   The original page is overwritten only after the entire operation
//   (including any compaction) succeeds and the candidate is validated.
//   This guarantees byte-for-byte preservation of the original on any error.
//
// Aliasing policy:
//   [new_data, new_data+new_size) may overlap [page, page+kPageSize).
//   We copy the source bytes into a local buffer immediately, before
//   any modification of the scratch page, so the source is always valid.

Result<RecordID> PageManager::UpdateRecord(std::byte*       page,
                                           RecordID         rid,
                                           const std::byte* new_data,
                                           std::size_t      new_size) noexcept {
    if (page == nullptr || new_data == nullptr) {
        return Result<RecordID>::Err(
            Status::InvalidArgument("page and new_data must not be null"));
    }

    // ── Phase 1: validate inputs (no page mutation) ──────────────────────────

    // Full page validation first — ensures we read consistent metadata.
    {
        Status v = ValidatePage(page);
        if (!v.IsOk()) return Result<RecordID>::Err(v);
    }

    PageHeader h{};
    ReadHeader(page, h);

    if (rid.page_id != h.page_id) {
        return Result<RecordID>::Err(Status::InvalidRecordID("page_id mismatch"));
    }
    if (!IsValidSlotID(h, rid.slot_id)) {
        return Result<RecordID>::Err(Status::InvalidRecordID("slot_id out of range"));
    }

    Slot s{};
    ReadSlot(page, rid.slot_id, s);

    if (IsTombstone(s)) {
        return Result<RecordID>::Err(Status::RecordNotFound("slot is a tombstone"));
    }

    {
        Status vs = ValidateLiveSlot(s);
        if (!vs.IsOk()) return Result<RecordID>::Err(vs);
    }

    if (new_size == 0) {
        return Result<RecordID>::Err(Status::InvalidArgument("new_size must be > 0"));
    }

    constexpr std::size_t kMaxUpdate = kPageSize - kHeaderSize;
    if (new_size > kMaxUpdate) {
        return Result<RecordID>::Err(
            Status::InvalidArgument("new_size exceeds page capacity"));
    }

    // ── Phase 2: capture source bytes into a safe local buffer ───────────────
    // This handles ALL aliasing cases:
    //   A. source entirely outside page — memcpy is trivially safe.
    //   B. source entirely inside page — bytes copied before any mutation.
    //   C/D. source partially overlaps page — copy the overlapping slice;
    //        bytes outside the page are untouched by our memcpy.
    // We always copy new_size bytes.  new_size <= kPageSize, so src_buf suffices.
    std::byte src_buf[kPageSize];
    if (RangeOverlapsPage(page, new_data, new_size)) {
        // Source overlaps page — copy it now, before any page modification.
        std::memcpy(src_buf, new_data, new_size);
    } else {
        // Source is external — copy into src_buf for uniform handling.
        std::memcpy(src_buf, new_data, new_size);
    }
    // src_buf[0..new_size) now holds the authoritative source bytes.

    const uint16_t old_size = s.length;

    // ── Phase 3: build the candidate result on a scratch copy ────────────────
    // All modifications (in-place or relocation + compaction) are applied to
    // scratch.  The original page is committed only on full success.
    std::byte scratch[kPageSize];
    std::memcpy(scratch, page, kPageSize);

    // Work exclusively on 'scratch' from here on.

    if (new_size <= static_cast<std::size_t>(old_size)) {
        // ── Case A: in-place update ──────────────────────────────────────────
        // Write new bytes at the record's existing offset in scratch.
        std::memcpy(scratch + s.offset, src_buf, new_size);
        // Update the slot length.
        Slot updated = s;
        updated.length = static_cast<uint16_t>(new_size);
        WriteSlot(scratch, rid.slot_id, updated);
        // Commit.
        std::memcpy(page, scratch, kPageSize);
        return Result<RecordID>::Ok(rid);
    }

    // ── Case B: relocation — new record is larger than old ───────────────────

    // Helper lambda: compute contiguous free space in a scratch-sized buffer.
    auto FreeInBuf = [&](const std::byte* buf) -> uint16_t {
        PageHeader bh{};
        ReadHeader(buf, bh);
        if (bh.free_space_end < bh.free_space_start) return 0;
        return static_cast<uint16_t>(bh.free_space_end - bh.free_space_start);
    };

    // Compact scratch if we don't have enough contiguous space.
    if (static_cast<std::size_t>(FreeInBuf(scratch)) < new_size) {
        // Compact scratch into a second temporary buffer.
        std::byte compact_tmp[kPageSize];
        std::memcpy(compact_tmp, scratch, kPageSize);

        PageHeader orig_hdr{};
        ReadHeader(scratch, orig_hdr);

        uint16_t cfse = static_cast<uint16_t>(kPageSize);
        for (uint16_t i = 0; i < orig_hdr.slot_count; ++i) {
            Slot cs{};
            ReadSlot(scratch, i, cs);
            if (IsTombstone(cs)) continue;
            cfse -= cs.length;
            // Read from scratch (the source for compaction), write to compact_tmp.
            std::memcpy(compact_tmp + cfse, scratch + cs.offset, cs.length);
            Slot cu = cs;
            cu.offset = cfse;
            WriteSlot(compact_tmp, i, cu);
        }
        // Update free_space_end in compact_tmp's header.
        PageHeader ct_hdr{};
        ReadHeader(compact_tmp, ct_hdr);
        ct_hdr.free_space_end = cfse;
        WriteHeader(compact_tmp, ct_hdr);

        // Validate the compacted candidate.
        {
            Status v = ValidatePage(compact_tmp);
            if (!v.IsOk()) return Result<RecordID>::Err(v);
        }

        // Promote compacted candidate to scratch.
        std::memcpy(scratch, compact_tmp, kPageSize);
    }

    // Re-read header from scratch after possible compaction.
    ReadHeader(scratch, h);

    // Still not enough space?
    if (static_cast<std::size_t>(FreeInBuf(scratch)) < new_size) {
        // Original page is untouched — return kPageFull.
        return Result<RecordID>::Err(
            Status::PageFull("insufficient space for record relocation"));
    }

    // Place the new record bytes at scratch's free_space_end.
    h.free_space_end -= static_cast<uint16_t>(new_size);
    std::memcpy(scratch + h.free_space_end, src_buf, new_size);

    // Update the existing slot in scratch — slot_id and RecordID preserved.
    // Re-read slot from scratch (compaction may have changed its offset field).
    ReadSlot(scratch, rid.slot_id, s);
    s.offset = h.free_space_end;
    s.length = static_cast<uint16_t>(new_size);
    WriteSlot(scratch, rid.slot_id, s);
    WriteHeader(scratch, h);

    // Validate the final candidate.
    {
        Status v = ValidatePage(scratch);
        if (!v.IsOk()) return Result<RecordID>::Err(v);
    }

    // ── Commit: overwrite original page only after full success ───────────────
    std::memcpy(page, scratch, kPageSize);

    return Result<RecordID>::Ok(rid);
}

// ── Compact ───────────────────────────────────────────────────────────────────

Status PageManager::Compact(std::byte* page, std::byte* scratch) noexcept {
    if (page == nullptr || scratch == nullptr) {
        return Status::InvalidArgument("page and scratch must not be null");
    }
    if (RegionsOverlap(page, scratch)) {
        return Status::InvalidArgument("page and scratch buffers overlap");
    }

    // Validate before touching anything.
    {
        Status v = ValidatePage(page);
        if (!v.IsOk()) return v;
    }

    // Build candidate in scratch.
    std::memcpy(scratch, page, kPageSize);

    PageHeader orig{};
    ReadHeader(page, orig);

    uint16_t sfse = static_cast<uint16_t>(kPageSize);

    for (uint16_t i = 0; i < orig.slot_count; ++i) {
        Slot s{};
        ReadSlot(page, i, s);  // always from the original page
        if (IsTombstone(s)) continue;

        sfse -= s.length;
        // Source: original page; destination: scratch.
        std::memcpy(scratch + sfse, page + s.offset, s.length);

        Slot updated = s;
        updated.offset = sfse;
        WriteSlot(scratch, i, updated);
    }

    PageHeader sh{};
    ReadHeader(scratch, sh);
    sh.free_space_end = sfse;
    WriteHeader(scratch, sh);

    // Validate candidate before committing.
    {
        Status v = ValidatePage(scratch);
        if (!v.IsOk()) return v;  // original page untouched
    }

    std::memcpy(page, scratch, kPageSize);
    return Status::Ok();
}

}  // namespace tinydb
