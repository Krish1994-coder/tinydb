#pragma once
// ============================================================
// TinyDB — Result<T>
// include/tinydb/common/result.h
//
// Result<T> represents either a successful value of type T or
// a Status describing the failure.
//
// Status and StatusCode are defined in status.h.
// ============================================================

#include <utility>

#include "tinydb/common/status.h"

namespace tinydb {

// ── Result<T> ─────────────────────────────────────────────────────────────

template <typename T>
class Result {
public:
    // Construct a successful result.
    static Result Ok(T value) {
        Result r;
        r.value_ = std::move(value);
        r.status_ = Status::Ok();
        return r;
    }

    // Construct a failed result.
    static Result Err(Status s) {
        Result r;
        r.status_ = std::move(s);
        return r;
    }

    bool IsOk() const noexcept {
        return status_.IsOk();
    }

    const Status& GetStatus() const noexcept {
        return status_;
    }

    T& Value() noexcept {
        return value_;
    }

    const T& Value() const noexcept {
        return value_;
    }

private:
    Status status_;
    T value_{};
};

}  // namespace tinydb
