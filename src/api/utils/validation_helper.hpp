#ifndef API_UTILS_VALIDATION_HELPER_HPP
#define API_UTILS_VALIDATION_HELPER_HPP

#include <string>
#include <memory>
#include <functional>

namespace api {
namespace utils {

/**
 * @brief 参数验证辅助工具
 * 
 * 提供统一的参数验证方法，减少重复代码
 */
class ValidationHelper {
public:
    /**
     * @brief 验证字符串是否为空
     * @param value 要验证的字符串
     * @param field_name 字段名称（用于错误消息）
     * @return 是否为空
     */
    static bool IsEmpty(const std::string& value, const std::string& field_name = "");

    /**
     * @brief 验证指针是否为nullptr
     * @param ptr 要验证的指针
     * @param field_name 字段名称（用于错误消息）
     * @return 是否为nullptr
     */
    template<typename T>
    static bool IsNull(const std::shared_ptr<T>& ptr, const std::string& field_name = "");

    /**
     * @brief 验证并返回错误消息（如果验证失败）
     * @param condition 验证条件（true表示验证通过）
     * @param error_msg 错误消息
     * @return 如果验证失败返回错误消息，否则返回空字符串
     */
    static std::string Validate(bool condition, const std::string& error_msg);

    /**
     * @brief 验证必需字段（字符串）
     * @param value 字段值
     * @param field_name 字段名称
     * @return 如果字段为空返回错误消息，否则返回空字符串
     */
    static std::string ValidateRequired(const std::string& value, const std::string& field_name);

    /**
     * @brief 验证必需字段（指针）
     * @param ptr 指针
     * @param field_name 字段名称
     * @return 如果指针为null返回错误消息，否则返回空字符串
     */
    template<typename T>
    static std::string ValidateRequired(const std::shared_ptr<T>& ptr, const std::string& field_name);
};

// 模板实现
template<typename T>
bool ValidationHelper::IsNull(const std::shared_ptr<T>& ptr, const std::string& field_name) {
    return ptr == nullptr;
}

template<typename T>
std::string ValidationHelper::ValidateRequired(const std::shared_ptr<T>& ptr, const std::string& field_name) {
    if (ptr == nullptr) {
        return field_name + " is required";
    }
    return "";
}

} // namespace utils
} // namespace api

#endif // API_UTILS_VALIDATION_HELPER_HPP

