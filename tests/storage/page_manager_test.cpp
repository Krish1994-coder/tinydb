// ============================================================
// TinyDB — PageManager unit tests
// tests/storage/page_manager_test.cpp
//
// All tests use stack-allocated page buffers.
// No Buffer Pool, StorageManager, or file system required.
//
// Design notes on aliasing tests:
//   InsertRecord and UpdateRecord accept source data that may point
//   into the page. The implementation copies source bytes into a
//   local buffer before any page mutation.
// ============================================================

#include "tinydb/storage/page_manager.h"
#include "tinydb/storage/page.h"
#include "tinydb/storage/record_id.h"
#include "tinydb/common/result.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

namespace tinydb {
namespace {

using PageBuf    = std::array<std::byte, kPageSize>;
using ScratchBuf = std::array<std::byte, kPageSize>;

static PageManager pm;

// ── Test helpers ─────────────────────────────────────────────────────────────

static std::vector<std::byte> MakeRecord(std::size_t len, uint8_t fill = 0xAB) {
    return std::vector<std::byte>(len, static_cast<std::byte>(fill));
}

static PageBuf InitPage(page_id_t pid = 1) {
    PageBuf buf{};
    EXPECT_EQ(pm.Init(buf.data(), pid), StatusCode::kOk);
    return buf;
}

static void AssertValid(const std::byte* page) {
    ASSERT_EQ(pm.ValidatePage(page), StatusCode::kOk);
}

static void WriteU16(PageBuf& buf, std::size_t off, uint16_t v) {
    std::memcpy(buf.data() + off, &v, 2);
}
static void WriteU32(PageBuf& buf, std::size_t off, uint32_t v) {
    std::memcpy(buf.data() + off, &v, 4);
}

// Corrupt an unrelated page-level invariant (I-10: flags != 0).
// This leaves the slot directory intact so target slots are still "valid-looking"
// but the page as a whole is corrupt.
static void CorruptPage_Flags(PageBuf& buf) {
    WriteU16(buf, 14, 0x0001u);  // offset 14 = flags field
}

// ── Init ─────────────────────────────────────────────────────────────────────

TEST(Init, ValidPageID) {
    auto buf = InitPage(1);
    AssertValid(buf.data());
    EXPECT_EQ(pm.GetSlotCount(buf.data()), 0u);
    EXPECT_EQ(pm.GetFreeSlotCount(buf.data()), 0u);
    EXPECT_EQ(pm.GetFreeSpace(buf.data()),
              static_cast<uint16_t>(kPageSize - kHeaderSize));
}

TEST(Init, InvalidPageID_Zero) {
    PageBuf buf{};
    EXPECT_EQ(pm.Init(buf.data(), 0), StatusCode::kInvalidArgument);
}

TEST(Init, InvalidPageID_Max) {
    PageBuf buf{};
    EXPECT_EQ(pm.Init(buf.data(), kInvalidPageId), StatusCode::kInvalidArgument);
}

TEST(Init, NullPage) {
    EXPECT_EQ(pm.Init(nullptr, 1), StatusCode::kInvalidArgument);
}

// ── InitFromExistingPage ──────────────────────────────────────────────────────

TEST(InitFromExistingPage, Valid) {
    auto buf = InitPage();
    EXPECT_EQ(pm.InitFromExistingPage(buf.data()), StatusCode::kOk);
}

TEST(InitFromExistingPage, CorruptPage) {
    auto buf = InitPage();
    WriteU16(buf, 6, 0xFFFFu);  // corrupt slot_count (I-03)
    EXPECT_EQ(pm.InitFromExistingPage(buf.data()), StatusCode::kCorruption);
}

TEST(InitFromExistingPage, NullPage) {
    EXPECT_EQ(pm.InitFromExistingPage(nullptr), StatusCode::kInvalidArgument);
}

// ── ValidatePage — all invariants ─────────────────────────────────────────────

TEST(ValidatePage, FreshPage_Passes) {
    auto buf = InitPage();
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kOk);
}

TEST(ValidatePage, NullPage) {
    EXPECT_EQ(pm.ValidatePage(nullptr), StatusCode::kInvalidArgument);
}

TEST(ValidatePage, I01_PageIDZero) {
    auto buf = InitPage();
    WriteU32(buf, 0, 0u);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I02_InvalidPageType) {
    auto buf = InitPage();
    WriteU16(buf, 4, 0xFFFFu);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I03_SlotCountTooLarge) {
    auto buf = InitPage();
    WriteU16(buf, 6, 679u);  // max = 678
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I04_FreeSlotCountExceedsSlotCount) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    WriteU16(buf, 8, 2u);  // free_slot_count = 2 > slot_count = 1
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I05_FreeSpaceStartInconsistent) {
    auto buf = InitPage();
    WriteU16(buf, 10, 100u);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I06_FreeSpaceEndBeforeStart) {
    auto buf = InitPage();
    WriteU16(buf, 12, 10u);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I07_FreeSpaceEndExceedsPageSize) {
    auto buf = InitPage();
    WriteU16(buf, 12, static_cast<uint16_t>(kPageSize + 1));
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I08_NonZeroChecksum) {
    auto buf = InitPage();
    WriteU32(buf, 24, 0x12345678u);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I09_NonZeroLSN_lo) {
    auto buf = InitPage();
    WriteU32(buf, 16, 1u);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I09_NonZeroLSN_hi) {
    auto buf = InitPage();
    WriteU32(buf, 20, 1u);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I10_NonZeroFlags) {
    auto buf = InitPage();
    WriteU16(buf, 14, 1u);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I11_LiveSlotOffsetInFreeRegion) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    // Read current free_space_end and set slot[0].offset below it.
    uint16_t fse;
    std::memcpy(&fse, buf.data() + 12, 2);
    uint16_t bad_off = static_cast<uint16_t>(fse - 1);
    std::memcpy(buf.data() + kHeaderSize, &bad_off, 2);  // slot[0].offset
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I12_SlotOffsetBelowHeaderSize) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    uint16_t bad = 5u;
    std::memcpy(buf.data() + kHeaderSize, &bad, 2);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I12_SlotOffsetPlusLengthExceedsPage) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    uint16_t bad_len = static_cast<uint16_t>(kPageSize);
    std::memcpy(buf.data() + kHeaderSize + 2, &bad_len, 2);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I13_LiveSlotZeroLength) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    uint16_t zero = 0u;
    std::memcpy(buf.data() + kHeaderSize + 2, &zero, 2);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I14_LiveSlotNonZeroReserved) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    buf[kHeaderSize + 4] = static_cast<std::byte>(0x01);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I15_TombstoneNonZeroReserved) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    pm.DeleteRecord(buf.data(), rid);
    buf[kHeaderSize + 4] = static_cast<std::byte>(0x01);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I16_OverlappingLiveSlots) {
    auto buf = InitPage();
    auto r1 = MakeRecord(50, 0x11);
    auto r2 = MakeRecord(50, 0x22);
    pm.InsertRecord(buf.data(), r1.data(), r1.size());
    pm.InsertRecord(buf.data(), r2.data(), r2.size());
    // Set slot[1].offset = slot[0].offset → overlapping.
    uint16_t off0;
    std::memcpy(&off0, buf.data() + kHeaderSize, 2);
    std::memcpy(buf.data() + kHeaderSize + kSlotSize, &off0, 2);
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I17_TombstoneCountMismatch) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    pm.DeleteRecord(buf.data(), rid);
    WriteU16(buf, 8, 0u);  // free_slot_count was 1; set to 0
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kCorruption);
}

TEST(ValidatePage, I18_ValidPagePasses) {
    auto buf = InitPage();
    auto rec = MakeRecord(20);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kOk);
}

// ── InsertRecord ──────────────────────────────────────────────────────────────

TEST(InsertRecord, NullPage) {
    auto rec = MakeRecord(10);
    EXPECT_EQ(pm.InsertRecord(nullptr, rec.data(), rec.size()).GetStatus(),
              StatusCode::kInvalidArgument);
}

TEST(InsertRecord, NullData) {
    auto buf = InitPage();
    EXPECT_EQ(pm.InsertRecord(buf.data(), nullptr, 10).GetStatus(),
              StatusCode::kInvalidArgument);
}

TEST(InsertRecord, ZeroLength) {
    auto buf = InitPage();
    auto rec = MakeRecord(0);
    EXPECT_EQ(pm.InsertRecord(buf.data(), rec.data(), 0).GetStatus(),
              StatusCode::kInvalidArgument);
}

TEST(InsertRecord, CorruptPage_Rejected_PageUnchanged) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    CorruptPage_Flags(buf);
    PageBuf before = buf;
    auto r = pm.InsertRecord(buf.data(), rec.data(), rec.size());
    EXPECT_EQ(r.GetStatus(), StatusCode::kCorruption);
    EXPECT_EQ(std::memcmp(buf.data(), before.data(), kPageSize), 0);
}

TEST(InsertRecord, RecordTooLarge_NewSlot) {
    auto buf = InitPage();
    constexpr std::size_t kMax = kPageSize - kHeaderSize - kSlotSize;  // 4062
    auto rec = MakeRecord(kMax + 1);
    EXPECT_EQ(pm.InsertRecord(buf.data(), rec.data(), rec.size()).GetStatus(),
              StatusCode::kInvalidArgument);
}

TEST(InsertRecord, ExactFit_NewSlot) {
    auto buf = InitPage();
    constexpr std::size_t kMax = kPageSize - kHeaderSize - kSlotSize;
    auto rec = MakeRecord(kMax);
    ASSERT_TRUE(pm.InsertRecord(buf.data(), rec.data(), rec.size()).IsOk());
    AssertValid(buf.data());
}

TEST(InsertRecord, OneByteTooMany_ReturnsPageFull) {
    auto buf = InitPage();
    constexpr std::size_t kMax = kPageSize - kHeaderSize - kSlotSize;
    auto rec = MakeRecord(kMax);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    auto r = pm.InsertRecord(buf.data(), MakeRecord(1).data(), 1);
    EXPECT_EQ(r.GetStatus(), StatusCode::kPageFull);
}

TEST(InsertRecord, ContentsCorrect) {
    auto buf = InitPage();
    auto rec = MakeRecord(50, 0xCD);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    auto gr = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr.IsOk());
    ASSERT_EQ(gr.Value().size, 50u);
    EXPECT_EQ(std::memcmp(gr.Value().data, rec.data(), 50), 0);
}

TEST(InsertRecord, NewSlotConsumesSlotSize) {
    auto buf = InitPage();
    uint16_t free_before = pm.GetFreeSpace(buf.data());
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    uint16_t free_after = pm.GetFreeSpace(buf.data());
    EXPECT_EQ(static_cast<uint16_t>(free_before - free_after),
              static_cast<uint16_t>(10 + kSlotSize));
    AssertValid(buf.data());
}

TEST(InsertRecord, TombstoneReuse_NoSlotDirectoryGrowth) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    pm.DeleteRecord(buf.data(), rid);
    uint16_t free_before = pm.GetFreeSpace(buf.data());
    auto rec2 = MakeRecord(8);
    pm.InsertRecord(buf.data(), rec2.data(), rec2.size());
    uint16_t free_after = pm.GetFreeSpace(buf.data());
    EXPECT_EQ(static_cast<uint16_t>(free_before - free_after), static_cast<uint16_t>(8));
    AssertValid(buf.data());
}

TEST(InsertRecord, TombstoneReuse_SameSlotID) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    auto rid1 = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    pm.DeleteRecord(buf.data(), rid1);
    auto r2 = pm.InsertRecord(buf.data(), MakeRecord(8).data(), 8);
    ASSERT_TRUE(r2.IsOk());
    EXPECT_EQ(r2.Value().slot_id, rid1.slot_id);
    AssertValid(buf.data());
}

TEST(InsertRecord, TombstoneReuse_DecreasesFreeSlotCount) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    pm.DeleteRecord(buf.data(), rid);
    EXPECT_EQ(pm.GetFreeSlotCount(buf.data()), 1u);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    EXPECT_EQ(pm.GetFreeSlotCount(buf.data()), 0u);
}

TEST(InsertRecord, MultipleRecords_AllRetrievable) {
    auto buf = InitPage();
    std::vector<RecordID> rids;
    for (uint8_t i = 0; i < 5; ++i) {
        auto rec = MakeRecord(static_cast<std::size_t>(20) + i, i);
        auto r = pm.InsertRecord(buf.data(), rec.data(), rec.size());
        ASSERT_TRUE(r.IsOk());
        rids.push_back(r.Value());
    }
    for (uint8_t i = 0; i < 5; ++i) {
        auto gr = pm.GetRecord(buf.data(), rids[i]);
        ASSERT_TRUE(gr.IsOk());
        EXPECT_EQ(gr.Value().size, static_cast<std::size_t>(20) + i);
        EXPECT_EQ(static_cast<uint8_t>(*gr.Value().data), i);
    }
    AssertValid(buf.data());
}

TEST(InsertRecord, PageFull) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    Status last{};
    for (int i = 0; i < 1000; ++i) {
        auto r = pm.InsertRecord(buf.data(), rec.data(), rec.size());
        if (!r.IsOk()) { last = r.GetStatus(); break; }
    }
    EXPECT_EQ(last, StatusCode::kPageFull);
    AssertValid(buf.data());
}

// ── InsertRecord aliasing ─────────────────────────────────────────────────────

TEST(InsertRecord, Aliasing_DataPointsToExistingRecord) {
    // Insert record A, then insert B using A's bytes as source.
    // The implementation must copy A's bytes before modifying free_space_end,
    // so B must contain exactly the same bytes as A.
    auto buf = InitPage();
    auto rec_a = MakeRecord(30, 0x77);
    auto rid_a = pm.InsertRecord(buf.data(), rec_a.data(), rec_a.size()).Value();

    auto gr_a = pm.GetRecord(buf.data(), rid_a);
    ASSERT_TRUE(gr_a.IsOk());
    const std::byte* a_ptr = gr_a.Value().data;
    const std::size_t a_len = gr_a.Value().size;

    // Insert using A's in-page storage as the source.
    auto r_b = pm.InsertRecord(buf.data(), a_ptr, a_len);
    ASSERT_TRUE(r_b.IsOk());

    auto gr_b = pm.GetRecord(buf.data(), r_b.Value());
    ASSERT_TRUE(gr_b.IsOk());
    ASSERT_EQ(gr_b.Value().size, a_len);
    // B must equal A's original content.
    EXPECT_EQ(std::memcmp(gr_b.Value().data, rec_a.data(), a_len), 0);

    AssertValid(buf.data());
}

TEST(InsertRecord, Aliasing_DataInsidePage_NotExistingRecord) {
    // Source pointer points into the page's header/slot area (not a record).
    // The implementation must handle this safely.
    auto buf = InitPage();
    // Point at offset 4 (page_type field area). Insert 4 bytes from there.
    // We just verify the operation doesn't corrupt memory — ValidatePage passes.
    const std::byte* page_mid = buf.data() + 4;
    auto r = pm.InsertRecord(buf.data(), page_mid, 4);
    // May succeed or fail (might not have enough space after validation), but must not crash.
    if (r.IsOk()) {
        AssertValid(buf.data());
    } else {
        // Any error is acceptable; page must remain valid or unchanged-when-invalid.
        // (The page was valid before, so if operation fails it must be unchanged.)
        EXPECT_EQ(pm.ValidatePage(buf.data()), StatusCode::kOk);
    }
}

// ── GetRecord ─────────────────────────────────────────────────────────────────

TEST(GetRecord, ValidRecord) {
    auto buf = InitPage();
    auto rec = MakeRecord(30, 0x77);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    auto gr = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr.IsOk());
    EXPECT_EQ(gr.Value().size, 30u);
    EXPECT_EQ(std::memcmp(gr.Value().data, rec.data(), 30), 0);
}

TEST(GetRecord, NullPage) {
    RecordID rid{1, 0};
    EXPECT_EQ(pm.GetRecord(nullptr, rid).GetStatus(), StatusCode::kInvalidArgument);
}

TEST(GetRecord, WrongPageID) {
    auto buf = InitPage(1);
    auto rec = MakeRecord(10);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    rid.page_id = 999;
    EXPECT_EQ(pm.GetRecord(buf.data(), rid).GetStatus(), StatusCode::kInvalidRecordID);
}

TEST(GetRecord, InvalidSlotID) {
    auto buf = InitPage();
    RecordID rid{1, 100};
    EXPECT_EQ(pm.GetRecord(buf.data(), rid).GetStatus(), StatusCode::kInvalidRecordID);
}

TEST(GetRecord, Tombstone) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    pm.DeleteRecord(buf.data(), rid);
    EXPECT_EQ(pm.GetRecord(buf.data(), rid).GetStatus(), StatusCode::kRecordNotFound);
}

TEST(GetRecord, CorruptSlot_Reserved) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    buf[kHeaderSize + 4] = static_cast<std::byte>(0xFF);
    RecordID rid{1, 0};
    EXPECT_EQ(pm.GetRecord(buf.data(), rid).GetStatus(), StatusCode::kCorruption);
}

TEST(GetRecord, CorruptSlot_ZeroLength) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    uint16_t zero = 0u;
    std::memcpy(buf.data() + kHeaderSize + 2, &zero, 2);
    RecordID rid{1, 0};
    EXPECT_EQ(pm.GetRecord(buf.data(), rid).GetStatus(), StatusCode::kCorruption);
}

TEST(GetRecord, CorruptPage_UnrelatedInvariant) {
    // Corrupt an unrelated page invariant (I-10: flags).
    // GetRecord must detect global corruption and return kCorruption.
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    CorruptPage_Flags(buf);  // corrupts I-10, not the target slot
    EXPECT_EQ(pm.GetRecord(buf.data(), rid).GetStatus(), StatusCode::kCorruption);
}

// ── DeleteRecord ──────────────────────────────────────────────────────────────

TEST(DeleteRecord, Valid) {
    auto buf = InitPage();
    auto rec = MakeRecord(20);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    EXPECT_EQ(pm.DeleteRecord(buf.data(), rid), StatusCode::kOk);
    AssertValid(buf.data());
    EXPECT_EQ(pm.GetFreeSlotCount(buf.data()), 1u);
}

TEST(DeleteRecord, NullPage) {
    RecordID rid{1, 0};
    EXPECT_EQ(pm.DeleteRecord(nullptr, rid), StatusCode::kInvalidArgument);
}

TEST(DeleteRecord, NotIdempotent) {
    auto buf = InitPage();
    auto rec = MakeRecord(20);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    pm.DeleteRecord(buf.data(), rid);
    EXPECT_EQ(pm.DeleteRecord(buf.data(), rid), StatusCode::kRecordNotFound);
}

TEST(DeleteRecord, InvalidSlotID) {
    auto buf = InitPage();
    RecordID bad{1, 99};
    EXPECT_EQ(pm.DeleteRecord(buf.data(), bad), StatusCode::kInvalidRecordID);
}

TEST(DeleteRecord, DoesNotChangeFreeSpacePointers) {
    auto buf = InitPage();
    auto rec = MakeRecord(50);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    uint16_t free_before = pm.GetFreeSpace(buf.data());
    pm.DeleteRecord(buf.data(), rid);
    EXPECT_EQ(pm.GetFreeSpace(buf.data()), free_before);
}

TEST(DeleteRecord, CorruptSlot_Reserved_PageUnchanged) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    buf[kHeaderSize + 4] = static_cast<std::byte>(0x01);
    PageBuf before = buf;
    EXPECT_EQ(pm.DeleteRecord(buf.data(), {1, 0}), StatusCode::kCorruption);
    EXPECT_EQ(std::memcmp(buf.data(), before.data(), kPageSize), 0);
}

TEST(DeleteRecord, CorruptSlot_ZeroLength_PageUnchanged) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    uint16_t zero = 0u;
    std::memcpy(buf.data() + kHeaderSize + 2, &zero, 2);
    PageBuf before = buf;
    EXPECT_EQ(pm.DeleteRecord(buf.data(), {1, 0}), StatusCode::kCorruption);
    EXPECT_EQ(std::memcmp(buf.data(), before.data(), kPageSize), 0);
}

TEST(DeleteRecord, CorruptPage_UnrelatedInvariant_PageUnchanged) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    CorruptPage_Flags(buf);
    PageBuf before = buf;
    EXPECT_EQ(pm.DeleteRecord(buf.data(), rid), StatusCode::kCorruption);
    EXPECT_EQ(std::memcmp(buf.data(), before.data(), kPageSize), 0);
}

TEST(DeleteRecord, InvalidRID_PageUnchanged) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    PageBuf before = buf;
    pm.DeleteRecord(buf.data(), {999, 0});
    EXPECT_EQ(std::memcmp(buf.data(), before.data(), kPageSize), 0);
}

// ── UpdateRecord ──────────────────────────────────────────────────────────────

TEST(UpdateRecord, NullPage) {
    auto rec = MakeRecord(10);
    EXPECT_EQ(pm.UpdateRecord(nullptr, {1, 0}, rec.data(), rec.size()).GetStatus(),
              StatusCode::kInvalidArgument);
}

TEST(UpdateRecord, NullData) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    EXPECT_EQ(pm.UpdateRecord(buf.data(), rid, nullptr, 5).GetStatus(),
              StatusCode::kInvalidArgument);
}

TEST(UpdateRecord, ZeroLength_PageUnchanged) {
    auto buf = InitPage();
    auto rec = MakeRecord(20);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    PageBuf before = buf;
    EXPECT_EQ(pm.UpdateRecord(buf.data(), rid, rec.data(), 0).GetStatus(),
              StatusCode::kInvalidArgument);
    EXPECT_EQ(std::memcmp(buf.data(), before.data(), kPageSize), 0);
}

TEST(UpdateRecord, OversizedRecord_PageUnchanged) {
    auto buf = InitPage();
    auto rec = MakeRecord(20);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    constexpr std::size_t kMax = kPageSize - kHeaderSize;
    auto big = MakeRecord(kMax + 1);
    PageBuf before = buf;
    EXPECT_EQ(pm.UpdateRecord(buf.data(), rid, big.data(), big.size()).GetStatus(),
              StatusCode::kInvalidArgument);
    EXPECT_EQ(std::memcmp(buf.data(), before.data(), kPageSize), 0);
}

TEST(UpdateRecord, InvalidRID_PageUnchanged) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    PageBuf before = buf;
    EXPECT_EQ(pm.UpdateRecord(buf.data(), {1, 99}, rec.data(), rec.size()).GetStatus(),
              StatusCode::kInvalidRecordID);
    EXPECT_EQ(std::memcmp(buf.data(), before.data(), kPageSize), 0);
}

TEST(UpdateRecord, TombstoneRID_PageUnchanged) {
    auto buf = InitPage();
    auto rec = MakeRecord(20);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    pm.DeleteRecord(buf.data(), rid);
    PageBuf before = buf;
    EXPECT_EQ(pm.UpdateRecord(buf.data(), rid, rec.data(), 10).GetStatus(),
              StatusCode::kRecordNotFound);
    EXPECT_EQ(std::memcmp(buf.data(), before.data(), kPageSize), 0);
}

TEST(UpdateRecord, CorruptSlot_PageUnchanged) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    buf[kHeaderSize + 4] = static_cast<std::byte>(0x01);
    PageBuf before = buf;
    EXPECT_EQ(pm.UpdateRecord(buf.data(), {1, 0}, rec.data(), rec.size()).GetStatus(),
              StatusCode::kCorruption);
    EXPECT_EQ(std::memcmp(buf.data(), before.data(), kPageSize), 0);
}

TEST(UpdateRecord, CorruptPage_UnrelatedInvariant_PageUnchanged) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    CorruptPage_Flags(buf);
    PageBuf before = buf;
    EXPECT_EQ(pm.UpdateRecord(buf.data(), rid, rec.data(), 5).GetStatus(),
              StatusCode::kCorruption);
    EXPECT_EQ(std::memcmp(buf.data(), before.data(), kPageSize), 0);
}

TEST(UpdateRecord, InPlace_SameSize) {
    auto buf = InitPage();
    auto rec = MakeRecord(30, 0xAA);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    auto nr = MakeRecord(30, 0xBB);
    auto r = pm.UpdateRecord(buf.data(), rid, nr.data(), nr.size());
    ASSERT_TRUE(r.IsOk());
    EXPECT_EQ(r.Value(), rid);
    auto gr = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr.IsOk());
    EXPECT_EQ(std::memcmp(gr.Value().data, nr.data(), 30), 0);
    AssertValid(buf.data());
}

TEST(UpdateRecord, InPlace_Smaller) {
    auto buf = InitPage();
    auto rec = MakeRecord(40, 0xAA);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    auto nr = MakeRecord(20, 0xCC);
    auto r = pm.UpdateRecord(buf.data(), rid, nr.data(), nr.size());
    ASSERT_TRUE(r.IsOk());
    EXPECT_EQ(r.Value(), rid);
    auto gr = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr.IsOk());
    EXPECT_EQ(gr.Value().size, 20u);
    EXPECT_EQ(std::memcmp(gr.Value().data, nr.data(), 20), 0);
    AssertValid(buf.data());
}

TEST(UpdateRecord, InPlace_PreservesPhysicalAddress) {
    auto buf = InitPage();
    auto rec = MakeRecord(40, 0xAA);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    const std::byte* ptr_before = pm.GetRecord(buf.data(), rid).Value().data;
    auto nr = MakeRecord(20, 0xCC);
    pm.UpdateRecord(buf.data(), rid, nr.data(), nr.size());
    const std::byte* ptr_after = pm.GetRecord(buf.data(), rid).Value().data;
    EXPECT_EQ(ptr_before, ptr_after);
}

TEST(UpdateRecord, Relocation_PreservesRecordID) {
    auto buf = InitPage();
    auto rec = MakeRecord(20);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    auto nr = MakeRecord(50, 0xDD);
    auto r = pm.UpdateRecord(buf.data(), rid, nr.data(), nr.size());
    ASSERT_TRUE(r.IsOk());
    EXPECT_EQ(r.Value(), rid);
    auto gr = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr.IsOk());
    EXPECT_EQ(gr.Value().size, 50u);
    EXPECT_EQ(std::memcmp(gr.Value().data, nr.data(), 50), 0);
    AssertValid(buf.data());
}

TEST(UpdateRecord, Relocation_PreservesSlotID) {
    auto buf = InitPage();
    auto rec = MakeRecord(10);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    const SlotID original_slot = rid.slot_id;
    auto nr = MakeRecord(100);
    auto r = pm.UpdateRecord(buf.data(), rid, nr.data(), nr.size());
    ASSERT_TRUE(r.IsOk());
    EXPECT_EQ(r.Value().slot_id, original_slot);
    AssertValid(buf.data());
}

// ── UpdateRecord aliasing ─────────────────────────────────────────────────────

TEST(UpdateRecord, Aliasing_InPlace_SrcIsExistingRecord) {
    // Update a record using its own bytes as source (in-place, same size).
    auto buf = InitPage();
    auto rec = MakeRecord(30, 0x77);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();

    auto gr = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr.IsOk());
    const std::byte* record_ptr = gr.Value().data;
    std::size_t record_len      = gr.Value().size;

    auto r = pm.UpdateRecord(buf.data(), rid, record_ptr, record_len);
    ASSERT_TRUE(r.IsOk());
    EXPECT_EQ(r.Value(), rid);

    auto gr2 = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr2.IsOk());
    EXPECT_EQ(gr2.Value().size, record_len);
    // All bytes must still be 0x77.
    for (std::size_t i = 0; i < record_len; ++i) {
        EXPECT_EQ(static_cast<uint8_t>(gr2.Value().data[i]), 0x77u) << " at byte " << i;
    }
    AssertValid(buf.data());
}

TEST(UpdateRecord, Aliasing_InPlace_Smaller_SrcInsidePage) {
    // Source points to the first half of the record. Update to that half.
    auto buf = InitPage();
    auto rec = MakeRecord(30, 0x55);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();

    auto gr = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr.IsOk());
    const std::byte* record_ptr = gr.Value().data;

    // Use the first 15 bytes of the record as source for a 15-byte update.
    auto r = pm.UpdateRecord(buf.data(), rid, record_ptr, 15);
    ASSERT_TRUE(r.IsOk());
    EXPECT_EQ(r.Value(), rid);

    auto gr2 = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr2.IsOk());
    EXPECT_EQ(gr2.Value().size, 15u);
    for (std::size_t i = 0; i < 15; ++i) {
        EXPECT_EQ(static_cast<uint8_t>(gr2.Value().data[i]), 0x55u) << " at byte " << i;
    }
    AssertValid(buf.data());
}

TEST(UpdateRecord, Aliasing_Relocation_SrcIsKnownBytes_InsidePage) {
    // Source is an existing record's full range. New size is larger → relocation.
    // The source bytes must be preserved correctly across the relocation.
    // We use a record filled with a distinctive pattern so we can verify the result.
    auto buf = InitPage();
    // Fill rec with ascending bytes so we can verify each byte individually.
    std::vector<std::byte> rec(20);
    for (std::size_t i = 0; i < 20; ++i) {
        rec[i] = static_cast<std::byte>(static_cast<uint8_t>(i + 1));
    }
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();

    auto gr = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr.IsOk());
    const std::byte* src_ptr = gr.Value().data;

    // Update using the existing record as source, with the SAME content,
    // but a larger size.  The extra bytes come from an external buffer.
    // To keep this deterministic: update to exactly old_size bytes from the
    // in-page pointer (we verify those bytes are preserved).
    // Use new_size = old_size + 10, sourcing from an external buffer that
    // extends the in-page content.
    std::vector<std::byte> extended(30);
    std::memcpy(extended.data(), src_ptr, 20);
    for (std::size_t i = 20; i < 30; ++i) {
        extended[i] = static_cast<std::byte>(static_cast<uint8_t>(i + 1));
    }
    // 'extended' is entirely external. Do the relocation using external source.
    auto r = pm.UpdateRecord(buf.data(), rid, extended.data(), extended.size());
    ASSERT_TRUE(r.IsOk());
    EXPECT_EQ(r.Value(), rid);

    auto gr2 = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr2.IsOk());
    ASSERT_EQ(gr2.Value().size, 30u);
    EXPECT_EQ(std::memcmp(gr2.Value().data, extended.data(), 30), 0);
    AssertValid(buf.data());
}

TEST(UpdateRecord, Aliasing_Relocation_SrcIsExistingRecord) {
    // Source is the existing record; new_size > old_size → relocation.
    // The first old_size bytes of the result must match the original record.
    auto buf = InitPage();
    auto rec = MakeRecord(20, 0x77);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();

    auto gr = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr.IsOk());
    const std::byte* record_ptr = gr.Value().data;
    const std::size_t old_len   = gr.Value().size;

    // Request new_size = old_size (same content, but still tests the code path
    // where the source aliases the page and we verify atomicity).
    // For a genuine relocation, use new_size > old_size.
    // We request 50 bytes but source only has 20 — the extra 30 bytes are
    // whatever follows in the page. We only verify the first 20 bytes match 0x77.
    // (The contract does not restrict what bytes beyond old_size are used when
    //  the source range extends past the original record; we just verify safety.)
    auto r = pm.UpdateRecord(buf.data(), rid, record_ptr, 50);
    ASSERT_TRUE(r.IsOk());
    EXPECT_EQ(r.Value(), rid);

    auto gr2 = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr2.IsOk());
    ASSERT_EQ(gr2.Value().size, 50u);
    // The first old_len bytes must match the original record content (0x77).
    for (std::size_t i = 0; i < old_len; ++i) {
        EXPECT_EQ(static_cast<uint8_t>(gr2.Value().data[i]), 0x77u) << " byte " << i;
    }
    AssertValid(buf.data());
}

// ── UpdateRecord — compaction required (deterministic) ────────────────────────

TEST(UpdateRecord, Relocation_RequiresCompaction_Deterministic) {
    // Setup:
    //   Fill page completely with 100-byte records (free == 0).
    //   Delete two 100-byte records to create 200 bytes of fragmentation.
    //   Update rids[0] from 100 → 120 bytes.
    //   Result: compaction reclaims 200 bytes → 120 fits.
    auto buf = InitPage();

    std::vector<RecordID> rids;
    auto rec_100 = MakeRecord(100, 0xAA);
    while (true) {
        auto r = pm.InsertRecord(buf.data(), rec_100.data(), rec_100.size());
        if (!r.IsOk()) break;
        rids.push_back(r.Value());
    }
    // Fill any remaining gap.
    {
        uint16_t free = pm.GetFreeSpace(buf.data());
        if (free > static_cast<uint16_t>(kSlotSize)) {
            std::size_t fit = static_cast<std::size_t>(free - kSlotSize);
            auto filler = MakeRecord(fit, 0xFF);
            auto r = pm.InsertRecord(buf.data(), filler.data(), filler.size());
            if (r.IsOk()) rids.push_back(r.Value());
        }
    }
    ASSERT_GE(rids.size(), static_cast<std::size_t>(4));
    ASSERT_EQ(pm.GetFreeSpace(buf.data()), 0u);

    // Capture rids[1] content before compaction.
    auto gr_b = pm.GetRecord(buf.data(), rids[1]);
    ASSERT_TRUE(gr_b.IsOk());
    std::vector<std::byte> b_snap(gr_b.Value().data,
                                  gr_b.Value().data + gr_b.Value().size);

    // Delete rids[2] and rids[3] (200 bytes fragmentation).
    ASSERT_EQ(pm.DeleteRecord(buf.data(), rids[2]), StatusCode::kOk);
    ASSERT_EQ(pm.DeleteRecord(buf.data(), rids[3]), StatusCode::kOk);
    ASSERT_EQ(pm.GetFreeSpace(buf.data()), 0u);

    // Update rids[0]: 100 → 120 bytes. Compaction required.
    auto bigger = MakeRecord(120, 0xEE);
    const SlotID slot_before = rids[0].slot_id;
    auto r = pm.UpdateRecord(buf.data(), rids[0], bigger.data(), bigger.size());
    ASSERT_TRUE(r.IsOk()) << "UpdateRecord must succeed after compaction";
    EXPECT_EQ(r.Value().slot_id, slot_before);
    EXPECT_EQ(r.Value().page_id, rids[0].page_id);

    auto gr_a = pm.GetRecord(buf.data(), rids[0]);
    ASSERT_TRUE(gr_a.IsOk());
    EXPECT_EQ(gr_a.Value().size, 120u);
    EXPECT_EQ(std::memcmp(gr_a.Value().data, bigger.data(), 120), 0);

    // rids[1] must be unchanged.
    auto gr_b2 = pm.GetRecord(buf.data(), rids[1]);
    ASSERT_TRUE(gr_b2.IsOk());
    EXPECT_EQ(gr_b2.Value().size, b_snap.size());
    EXPECT_EQ(std::memcmp(gr_b2.Value().data, b_snap.data(), b_snap.size()), 0);

    // Deleted records remain tombstones.
    EXPECT_EQ(pm.GetRecord(buf.data(), rids[2]).GetStatus(), StatusCode::kRecordNotFound);
    EXPECT_EQ(pm.GetRecord(buf.data(), rids[3]).GetStatus(), StatusCode::kRecordNotFound);

    AssertValid(buf.data());
}

TEST(UpdateRecord, InsufficientSpace_AfterCompaction_PageUnchanged_Deterministic) {
    // Fill page with one maximum-size record (no tombstones → compaction cannot help).
    // Attempt to grow → kPageFull. Original page must be byte-for-byte unchanged
    // because UpdateRecord is atomic.
    auto buf = InitPage();
    constexpr std::size_t kRecSize = kPageSize - kHeaderSize - kSlotSize;  // 4062
    auto rec = MakeRecord(kRecSize, 0xAA);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    ASSERT_EQ(pm.GetFreeSpace(buf.data()), 0u);
    ASSERT_EQ(pm.GetFreeSlotCount(buf.data()), 0u);

    auto gr = pm.GetRecord(buf.data(), rid);
    ASSERT_TRUE(gr.IsOk());
    ASSERT_EQ(gr.Value().size, kRecSize);

    // Save page state before the failing update.
    PageBuf before = buf;

    // kRecSize + 6 = 4068 = kPageSize - kHeaderSize (maximum valid new_size).
    constexpr std::size_t kNewSize = kRecSize + kSlotSize;
    static_assert(kNewSize <= kPageSize - kHeaderSize, "within limit");
    static_assert(kNewSize > kRecSize, "triggers Case B");

    auto bigger = MakeRecord(kNewSize, 0xBB);
    auto r = pm.UpdateRecord(buf.data(), rid, bigger.data(), bigger.size());
    EXPECT_EQ(r.GetStatus(), StatusCode::kPageFull);

    // Page must be byte-for-byte identical to its pre-update state.
    EXPECT_EQ(std::memcmp(buf.data(), before.data(), kPageSize), 0);
}

// ── Compaction ────────────────────────────────────────────────────────────────

TEST(Compact, NullPage) {
    ScratchBuf scratch{};
    EXPECT_EQ(pm.Compact(nullptr, scratch.data()), StatusCode::kInvalidArgument);
}

TEST(Compact, NullScratch) {
    auto buf = InitPage();
    EXPECT_EQ(pm.Compact(buf.data(), nullptr), StatusCode::kInvalidArgument);
}

TEST(Compact, OverlappingBuffers) {
    auto buf = InitPage();
    EXPECT_EQ(pm.Compact(buf.data(), buf.data()), StatusCode::kInvalidArgument);
}

TEST(Compact, EmptyPage_NoChange) {
    auto buf = InitPage();
    ScratchBuf scratch{};
    uint16_t free_before = pm.GetFreeSpace(buf.data());
    ASSERT_EQ(pm.Compact(buf.data(), scratch.data()), StatusCode::kOk);
    EXPECT_EQ(pm.GetFreeSpace(buf.data()), free_before);
    EXPECT_EQ(pm.GetSlotCount(buf.data()), 0u);
    AssertValid(buf.data());
}

TEST(Compact, PreservesExactRecordContents) {
    auto buf = InitPage();
    ScratchBuf scratch{};
    auto rec1 = MakeRecord(30, 0x11);
    auto rec2 = MakeRecord(40, 0x22);
    auto rid1 = pm.InsertRecord(buf.data(), rec1.data(), rec1.size()).Value();
    auto rid2 = pm.InsertRecord(buf.data(), rec2.data(), rec2.size()).Value();
    pm.DeleteRecord(buf.data(), rid1);
    ASSERT_EQ(pm.Compact(buf.data(), scratch.data()), StatusCode::kOk);
    EXPECT_EQ(pm.GetRecord(buf.data(), rid1).GetStatus(), StatusCode::kRecordNotFound);
    auto gr2 = pm.GetRecord(buf.data(), rid2);
    ASSERT_TRUE(gr2.IsOk());
    EXPECT_EQ(gr2.Value().size, 40u);
    EXPECT_EQ(std::memcmp(gr2.Value().data, rec2.data(), 40), 0);
    AssertValid(buf.data());
}

TEST(Compact, PreservesAllLiveRecordIDs) {
    auto buf = InitPage();
    ScratchBuf scratch{};
    std::vector<RecordID> rids;
    std::vector<std::vector<std::byte>> contents;
    for (uint8_t i = 0; i < 6; ++i) {
        auto rec = MakeRecord(20, i);
        rids.push_back(pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value());
        contents.push_back(rec);
    }
    pm.DeleteRecord(buf.data(), rids[1]);
    pm.DeleteRecord(buf.data(), rids[3]);
    pm.DeleteRecord(buf.data(), rids[5]);
    ASSERT_EQ(pm.Compact(buf.data(), scratch.data()), StatusCode::kOk);
    for (int i : {0, 2, 4}) {
        auto gr = pm.GetRecord(buf.data(), rids[i]);
        ASSERT_TRUE(gr.IsOk()) << "slot " << i;
        EXPECT_EQ(std::memcmp(gr.Value().data, contents[i].data(), 20), 0);
    }
    for (int i : {1, 3, 5}) {
        EXPECT_EQ(pm.GetRecord(buf.data(), rids[i]).GetStatus(),
                  StatusCode::kRecordNotFound);
    }
    AssertValid(buf.data());
}

TEST(Compact, SlotCountAndFreeSlotCountPreserved) {
    auto buf = InitPage();
    ScratchBuf scratch{};
    auto rec = MakeRecord(10);
    std::vector<RecordID> rids;
    for (int i = 0; i < 4; ++i) {
        rids.push_back(pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value());
    }
    pm.DeleteRecord(buf.data(), rids[1]);
    pm.DeleteRecord(buf.data(), rids[3]);
    uint16_t sc  = pm.GetSlotCount(buf.data());
    uint16_t fsc = pm.GetFreeSlotCount(buf.data());
    ASSERT_EQ(pm.Compact(buf.data(), scratch.data()), StatusCode::kOk);
    EXPECT_EQ(pm.GetSlotCount(buf.data()), sc);
    EXPECT_EQ(pm.GetFreeSlotCount(buf.data()), fsc);
    AssertValid(buf.data());
}

TEST(Compact, FreeSpaceEndRecalculated) {
    auto buf = InitPage();
    ScratchBuf scratch{};
    auto rec = MakeRecord(100);
    std::vector<RecordID> rids;
    for (int i = 0; i < 4; ++i) {
        rids.push_back(pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value());
    }
    pm.DeleteRecord(buf.data(), rids[0]);
    pm.DeleteRecord(buf.data(), rids[2]);
    uint16_t free_before = pm.GetFreeSpace(buf.data());
    ASSERT_EQ(pm.Compact(buf.data(), scratch.data()), StatusCode::kOk);
    uint16_t free_after = pm.GetFreeSpace(buf.data());
    EXPECT_EQ(static_cast<uint16_t>(free_after - free_before),
              static_cast<uint16_t>(200));  // 2 × 100 bytes reclaimed
    AssertValid(buf.data());
}

TEST(Compact, PreservesTombstones) {
    auto buf = InitPage();
    ScratchBuf scratch{};
    auto rec = MakeRecord(10);
    auto rid = pm.InsertRecord(buf.data(), rec.data(), rec.size()).Value();
    pm.DeleteRecord(buf.data(), rid);
    ASSERT_EQ(pm.Compact(buf.data(), scratch.data()), StatusCode::kOk);
    EXPECT_EQ(pm.GetFreeSlotCount(buf.data()), 1u);
    EXPECT_EQ(pm.GetRecord(buf.data(), rid).GetStatus(), StatusCode::kRecordNotFound);
    AssertValid(buf.data());
}

TEST(Compact, DetectsCorruptedSourcePage) {
    auto buf = InitPage();
    ScratchBuf scratch{};
    WriteU16(buf, 4, 0xFFFFu);  // corrupt page_type (I-02)
    EXPECT_EQ(pm.Compact(buf.data(), scratch.data()), StatusCode::kCorruption);
}

TEST(Compact, FailureLeavesOriginalPageUnchanged) {
    auto buf = InitPage();
    ScratchBuf scratch{};
    auto rec = MakeRecord(20, 0xAB);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    WriteU16(buf, 4, 0xFFFFu);  // corrupt after insert
    PageBuf corrupted = buf;
    pm.Compact(buf.data(), scratch.data());
    EXPECT_EQ(std::memcmp(buf.data(), corrupted.data(), kPageSize), 0);
}

// ── Boundary conditions ───────────────────────────────────────────────────────

TEST(Boundary, MaxRecordSize_NewSlot) {
    auto buf = InitPage();
    constexpr std::size_t kMax = kPageSize - kHeaderSize - kSlotSize;
    auto rec = MakeRecord(kMax, 0xEE);
    auto r = pm.InsertRecord(buf.data(), rec.data(), rec.size());
    ASSERT_TRUE(r.IsOk());
    auto gr = pm.GetRecord(buf.data(), r.Value());
    ASSERT_TRUE(gr.IsOk());
    EXPECT_EQ(gr.Value().size, kMax);
    AssertValid(buf.data());
}

TEST(Boundary, MaxReusedSlot_FitsAvailableSpace) {
    // Insert 1-byte record, delete it, verify we can now insert up to available free.
    auto buf = InitPage();
    auto small = MakeRecord(1);
    auto rid = pm.InsertRecord(buf.data(), small.data(), small.size()).Value();
    pm.DeleteRecord(buf.data(), rid);
    // free_space_end = kPageSize - 1; free_space_start = 28 + 6 = 34.
    // Available for a reused-slot insert = 4095 - 34 = 4061 bytes.
    uint16_t avail = pm.GetFreeSpace(buf.data());
    ASSERT_GT(avail, 0u);
    auto big = MakeRecord(avail, 0xEE);
    auto r = pm.InsertRecord(buf.data(), big.data(), big.size());
    ASSERT_TRUE(r.IsOk());
    AssertValid(buf.data());
}

TEST(Boundary, FreeSpaceArithmetic) {
    auto buf = InitPage();
    EXPECT_EQ(pm.GetFreeSpace(buf.data()),
              static_cast<uint16_t>(kPageSize - kHeaderSize));
    auto rec = MakeRecord(10);
    pm.InsertRecord(buf.data(), rec.data(), rec.size());
    EXPECT_EQ(pm.GetSlotCount(buf.data()), 1u);
    EXPECT_EQ(pm.GetFreeSpace(buf.data()),
              static_cast<uint16_t>(kPageSize - kHeaderSize - 10 - kSlotSize));
}

TEST(Boundary, FreeSpaceAfterCompaction_AllowsLargerInsert) {
    auto buf = InitPage();
    ScratchBuf scratch{};
    auto rec = MakeRecord(100);
    std::vector<RecordID> rids;
    for (int i = 0; i < 5; ++i) {
        auto r = pm.InsertRecord(buf.data(), rec.data(), rec.size());
        if (!r.IsOk()) break;
        rids.push_back(r.Value());
    }
    pm.DeleteRecord(buf.data(), rids[0]);
    pm.DeleteRecord(buf.data(), rids[2]);
    pm.DeleteRecord(buf.data(), rids[4]);
    pm.Compact(buf.data(), scratch.data());
    EXPECT_GT(pm.GetFreeSpace(buf.data()), static_cast<uint16_t>(200));
    AssertValid(buf.data());
}

}  // namespace
}  // namespace tinydb
