#ifndef COMMON_EXCEPTIONS_HPP
#define COMMON_EXCEPTIONS_HPP

#include <stdexcept>
#include <string>

namespace gateway {

/**
 * @brief Base class for all Gateway exceptions.
 * Includes an internal error code and a suggested HTTP status code.
 */
class GatewayException : public std::runtime_error {
public:
    GatewayException(const std::string& message, int code, int http_status = 500)
        : std::runtime_error(message), code_(code), http_status_(http_status) {}

    virtual ~GatewayException() = default;

    int code() const { return code_; }
    int http_status() const { return http_status_; }

private:
    int code_;
    int http_status_;
};

/**
 * @brief Exception for when a requested resource (device, stream, etc.) is not found.
 * HTTP 404
 */
class NotFoundException : public GatewayException {
public:
    NotFoundException(const std::string& message, int code = -1)
        : GatewayException(message, code, 404) {}
};

class DeviceNotFoundException : public NotFoundException {
public:
    DeviceNotFoundException(const std::string& device_id)
        : NotFoundException("Device not found: " + device_id, -1) {}
};

class StreamNotFoundException : public NotFoundException {
public:
    StreamNotFoundException(const std::string& app, const std::string& stream)
        : NotFoundException("Stream not found: " + app + "/" + stream, -1) {}
};

/**
 * @brief Exception for invalid client requests (bad JSON, missing fields, etc.).
 * HTTP 400
 */
class BadRequestException : public GatewayException {
public:
    BadRequestException(const std::string& message, int code = -1)
        : GatewayException(message, code, 400) {}
};

class InvalidParameterException : public BadRequestException {
public:
    InvalidParameterException(const std::string& param_name)
        : BadRequestException("Invalid or missing parameter: " + param_name, -1) {}
};

class JsonParseException : public BadRequestException {
public:
    JsonParseException(const std::string& details)
        : BadRequestException("JSON parse error: " + details, -1) {}
};

class InvalidRequestException : public BadRequestException {
public:
    InvalidRequestException(const std::string& details)
        : BadRequestException(details, -1) {}
};

/**
 * @brief Exception for conflicts (e.g., trying to create a resource that already exists).
 * HTTP 409
 */
class ConflictException : public GatewayException {
public:
    ConflictException(const std::string& message, int code = -1)
        : GatewayException(message, code, 409) {}
};

class StreamAlreadyExistsException : public ConflictException {
public:
    StreamAlreadyExistsException(const std::string& app, const std::string& stream)
        : ConflictException("Stream already exists: " + app + "/" + stream, -1) {}
};

class DeviceAlreadyExistsException : public ConflictException {
public:
    DeviceAlreadyExistsException(const std::string& device_id)
        : ConflictException("Device already exists: " + device_id, -1) {}
};

/**
 * @brief Exception for internal server errors.
 * HTTP 500
 */
class InternalServerException : public GatewayException {
public:
    InternalServerException(const std::string& message, int code = -1)
        : GatewayException(message, code, 500) {}
};

class FFmpegException : public InternalServerException {
public:
    FFmpegException(const std::string& details)
        : InternalServerException("FFmpeg error: " + details, -1) {}
};

class NetworkException : public InternalServerException {
public:
    NetworkException(const std::string& details)
        : InternalServerException("Network error: " + details, -1) {}
};

class DeviceConnectionException : public NetworkException {
public:
    DeviceConnectionException(const std::string& details)
        : NetworkException("Device connection error: " + details) {}
};

class TimeoutException : public InternalServerException {
public:
    TimeoutException(const std::string& operation)
        : InternalServerException("Operation timed out: " + operation, -1) {}
};

class ZLMAPIException : public InternalServerException {
public:
    ZLMAPIException(const std::string& details)
        : InternalServerException("ZLM API error: " + details, -1) {}
};

class SIPException : public InternalServerException {
public:
    SIPException(const std::string& details)
        : InternalServerException("SIP protocol error: " + details, -1) {}
};

} // namespace gateway

#endif // COMMON_EXCEPTIONS_HPP
