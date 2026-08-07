#pragma once

#include <cstdint>

namespace tinydb {

/// page_id_t is the type used to identify a page within a database file.
///
/// A page identifier is an unsigned 32-bit integer assigned at allocation
/// time. It is immutable for the lifetime of the page and unique within one
/// database file.
///
/// Identifiers are monotonically increasing in the initial implementation.
/// Once assigned, an identifier is never recycled (free-list reuse is a
/// Phase 3 extension).
using page_id_t = uint32_t;

/// The page identifier of the file header.
///
/// Page 0 always exists in a valid database file and always contains the
/// file header (magic number, format version, page size, page count).
/// No other layer may interpret or overwrite this page without going through
/// the StorageManager.
constexpr page_id_t HEADER_PAGE_ID = 0;

/// Sentinel value representing "no page" or an uninitialised page reference.
///
/// Used by higher layers (page manager, B+ tree) to indicate that a pointer
/// or slot does not currently refer to a valid page. The storage engine
/// rejects any I/O request for this identifier.
constexpr page_id_t INVALID_PAGE_ID = 0xFFFF'FFFF;

}  // namespace tinydb
