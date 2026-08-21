#pragma once
// ============================================================
// TinyDB — page_id_t and kInvalidPageId
// include/tinydb/storage/page_id.h
// ============================================================

#include <cstdint>
#include <limits>

namespace tinydb {

using page_id_t = uint32_t;

inline constexpr page_id_t kInvalidPageId =
    std::numeric_limits<uint32_t>::max();

}  // namespace tinydb
