#pragma once
// ============================================================
// TinyDB — ByteView
// include/tinydb/common/byte_view.h
//
// Non-owning view into a contiguous byte range.
// C++17 replacement for std::span<const std::byte> (C++20).
//
// Lifetime contract:
//   A ByteView returned by PageManager::GetRecord() is valid only while:
//     (1) the source page remains pinned in the Buffer Pool, AND
//     (2) no operation (DeleteRecord, UpdateRecord with relocation,
//         or Compact) invalidates the referenced record bytes.
//   Pinning prevents page eviction but does NOT prevent in-place
//   mutation of record bytes.
// ============================================================

#include <cstddef>

namespace tinydb {

struct ByteView {
    const std::byte* data{nullptr};
    std::size_t      size{0};

    const std::byte* begin() const noexcept { return data; }
    const std::byte* end()   const noexcept { return data + size; }
    bool             empty() const noexcept { return size == 0; }
};

}  // namespace tinydb
