#pragma once
// ============================================================
// TinyDB — Status
// include/tinydb/common/status.h
//
// Minimal error-handling abstraction used throughout TinyDB.
//
// Status represents success or a typed error with an optional
// descriptive message.
//
// Status codes used by the PageManager include page/record
// related errors. StorageManager additionally uses the
// storage-engine lifecycle and I/O related errors.
// ============================================================

#include <cstdint>
#include <string>
#include <utility>

namespace tinydb {

// ── StatusCode ────────────────────────────────────────────────────────────

enum class StatusCode : uint8_t {
    kOk              = 0,
    kInvalidArgument = 1,
    kRecordNotFound  = 2,
    kInvalidRecordID = 3,
    kPageFull        = 4,
    kCorruption      = 5,
    kIOError         = 6,
    kNotImplemented  = 7,

    // StorageManager errors.
    kAlreadyExists   = 8,
    kNotFound        = 9,
    kOutOfSpace      = 10,
    kNotOpen         = 11,
};

// ── Status ────────────────────────────────────────────────────────────────

class Status {
public:
    // Construct an Ok status.
    Status() : code_(StatusCode::kOk) {}

    // Construct a status with a code and optional message.
    explicit Status(StatusCode code) : code_(code) {}

    Status(StatusCode code, std::string msg)
        : code_(code), msg_(std::move(msg)) {}

    // Named constructors for common codes.
    static Status Ok() {
        return Status{};
    }

    static Status InvalidArgument(std::string m = {}) {
        return Status{StatusCode::kInvalidArgument, std::move(m)};
    }

    static Status RecordNotFound(std::string m = {}) {
        return Status{StatusCode::kRecordNotFound, std::move(m)};
    }

    static Status InvalidRecordID(std::string m = {}) {
        return Status{StatusCode::kInvalidRecordID, std::move(m)};
    }

    static Status PageFull(std::string m = {}) {
        return Status{StatusCode::kPageFull, std::move(m)};
    }

    static Status Corruption(std::string m = {}) {
        return Status{StatusCode::kCorruption, std::move(m)};
    }

    static Status IOError(std::string m = {}) {
        return Status{StatusCode::kIOError, std::move(m)};
    }

    static Status NotImplemented(std::string m = {}) {
        return Status{StatusCode::kNotImplemented, std::move(m)};
    }

    static Status AlreadyExists(std::string m = {}) {
        return Status{StatusCode::kAlreadyExists, std::move(m)};
    }

    static Status NotFound(std::string m = {}) {
        return Status{StatusCode::kNotFound, std::move(m)};
    }

    static Status OutOfSpace(std::string m = {}) {
        return Status{StatusCode::kOutOfSpace, std::move(m)};
    }

    static Status NotOpen(std::string m = {}) {
        return Status{StatusCode::kNotOpen, std::move(m)};
    }

    bool IsOk() const noexcept {
        return code_ == StatusCode::kOk;
    }

    StatusCode Code() const noexcept {
        return code_;
    }

    const std::string& Message() const noexcept {
        return msg_;
    }

    bool operator==(StatusCode c) const noexcept {
        return code_ == c;
    }

    bool operator!=(StatusCode c) const noexcept {
        return code_ != c;
    }

    bool operator==(const Status& rhs) const noexcept {
        return code_ == rhs.code_;
    }

    bool operator!=(const Status& rhs) const noexcept {
        return code_ != rhs.code_;
    }

private:
    StatusCode code_;
    std::string msg_;
};

}  // namespace tinydb
