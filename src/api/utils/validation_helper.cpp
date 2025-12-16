#include "api/utils/validation_helper.hpp"

namespace api {
namespace utils {

bool ValidationHelper::IsEmpty(const std::string& value, const std::string& field_name) {
    return value.empty();
}

std::string ValidationHelper::Validate(bool condition, const std::string& error_msg) {
    if (!condition) {
        return error_msg;
    }
    return "";
}

std::string ValidationHelper::ValidateRequired(const std::string& value, const std::string& field_name) {
    if (value.empty()) {
        return field_name + " is required";
    }
    return "";
}

} // namespace utils
} // namespace api

