#ifndef COMMON_RESULT_HPP
#define COMMON_RESULT_HPP

#include "common/exceptions.hpp"
#include <variant>
#include <string>
#include <optional>

namespace gateway {

/**
 * @brief A generic Result type that can hold either a value or an error.
 * @tparam T The type of the value.
 */
template<typename T>
class Result {
public:
    // Success constructor
    Result(const T& value) : value_(value), is_success_(true) {}
    Result(T&& value) : value_(std::move(value)), is_success_(true) {}

    // Error constructor
    Result(const GatewayException& error) : error_(error), is_success_(false) {}
    
    // Explicit static factories
    static Result<T> Success(const T& value) {
        return Result<T>(value);
    }
    static Result<T> Success(T&& value) {
        return Result<T>(std::move(value));
    }
    static Result<T> Failure(const GatewayException& error) {
        return Result<T>(error);
    }
    static Result<T> Failure(const std::string& msg, int code = -1) {
        return Result<T>(GatewayException(msg, code));
    }

    bool IsSuccess() const { return is_success_; }
    bool IsFailure() const { return !is_success_; }

    T& Value() {
        if (!is_success_) {
            throw error_;
        }
        return value_.value();
    }

    const T& Value() const {
        if (!is_success_) {
            throw error_;
        }
        return value_.value();
    }
    
    // Moves the value out. Useful for large objects.
    T Unwrapped() {
        if (!is_success_) {
            throw error_;
        }
        return std::move(value_.value());
    }

    const GatewayException& Error() const {
        return error_;
    }

private:
    std::optional<T> value_;
    GatewayException error_{"", 0}; // Default initialize just to satisfy compiler
    bool is_success_;
};

/**
 * @brief Specialization for void.
 */
template<>
class Result<void> {
public:
    Result() : is_success_(true) {}
    Result(const GatewayException& error) : error_(error), is_success_(false) {}

    static Result<void> Success() {
        return Result<void>();
    }
    static Result<void> Failure(const GatewayException& error) {
        return Result<void>(error);
    }
    static Result<void> Failure(const std::string& msg, int code = -1) {
        return Result<void>(GatewayException(msg, code));
    }

    bool IsSuccess() const { return is_success_; }
    bool IsFailure() const { return !is_success_; }

    void ThrowIfError() const {
        if (!is_success_) {
            throw error_;
        }
    }

    const GatewayException& Error() const {
        return error_;
    }

private:
    GatewayException error_{"", 0};
    bool is_success_;
};

} // namespace gateway

#endif // COMMON_RESULT_HPP
