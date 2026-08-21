#pragma once
// ============================================================
// TinyDB — Status and Result<T>
// include/tinydb/common/result.h
//
// Minimal error-handling abstraction used throughout TinyDB.
//
// Status  — represents success or a typed error with an
//            optional descriptive message.
// Result<T> — either a successful value of type T, or a Status
//             describing the failure.
//
// PageManager uses the following codes:
//   kOk              — operation succeeded
//   kInvalidArgument — bad input (size==0, aliasing, etc.)
//   kRecordNotFound  — slot is a tombstone
//   kInvalidRecordID — slot_id out of range, wrong page_id
//   kPageFull        — no space even after compaction
//   kCorruption      — page invariant violated
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
    static Status Ok()    { return Status{}; }

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

    bool       IsOk()    const noexcept { return code_ == StatusCode::kOk; }
    StatusCode Code()    const noexcept { return code_; }
    const std::string& Message() const noexcept { return msg_; }

    bool operator==(StatusCode c)      const noexcept { return code_ == c; }
    bool operator!=(StatusCode c)      const noexcept { return code_ != c; }
    bool operator==(const Status& rhs) const noexcept { return code_ == rhs.code_; }
    bool operator!=(const Status& rhs) const noexcept { return code_ != rhs.code_; }

private:
    StatusCode  code_;
    std::string msg_;
};

// ── Result<T> ─────────────────────────────────────────────────────────────

template <typename T>
class Result {
public:
    // Construct a successful result.
    static Result Ok(T value) {
        Result r;
        r.value_  = std::move(value);
        r.status_ = Status::Ok();
        return r;
    }

    // Construct a failed result.
    static Result Err(Status s) {
        Result r;
        r.status_ = std::move(s);
        return r;
    }

    bool          IsOk()      const noexcept { return status_.IsOk(); }
    const Status& GetStatus() const noexcept { return status_; }
    T&            Value()           noexcept { return value_; }
    const T&      Value()     const noexcept { return value_; }

private:
    Status status_;
    T      value_{};
};

}  // namespace tinydb
