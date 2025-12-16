#include "api/utils/response_helper.hpp"
#include "utils/logger.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace api {
namespace utils {

void ResponseHelper::Success(httplib::Response& res, const nlohmann::json& data, const std::string& msg) {
    try {
        json response = {
            {"code", 0},
            {"data", data}
        };
        if (!msg.empty()) {
            response["msg"] = msg;
        }
        std::string json_str = response.dump();
        res.set_content(json_str, "application/json");
    } catch (const json::exception& e) {
        ::utils::Logger::Get()->error("JSON serialization failed in ResponseHelper::Success: {}", e.what());
        res.status = 500;
        json error = {
            {"code", -1},
            {"msg", "Internal server error: JSON serialization failed"}
        };
        try {
            res.set_content(error.dump(), "application/json");
        } catch (...) {
            res.set_content("{\"code\":-1,\"msg\":\"Internal server error\"}", "application/json");
        }
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Unexpected error in ResponseHelper::Success: {}", e.what());
        res.status = 500;
        json error = {
            {"code", -1},
            {"msg", "Internal server error"}
        };
        try {
            res.set_content(error.dump(), "application/json");
        } catch (...) {
            res.set_content("{\"code\":-1,\"msg\":\"Internal server error\"}", "application/json");
        }
    }
}

void ResponseHelper::Success(httplib::Response& res, const std::string& msg) {
    try {
        json response = {
            {"code", 0}
        };
        if (!msg.empty()) {
            response["msg"] = msg;
        }
        std::string json_str = response.dump();
        res.set_content(json_str, "application/json");
    } catch (const json::exception& e) {
        ::utils::Logger::Get()->error("JSON serialization failed in ResponseHelper::Success: {}", e.what());
        res.status = 500;
        json error = {
            {"code", -1},
            {"msg", "Internal server error: JSON serialization failed"}
        };
        try {
            res.set_content(error.dump(), "application/json");
        } catch (...) {
            res.set_content("{\"code\":-1,\"msg\":\"Internal server error\"}", "application/json");
        }
    } catch (const std::exception& e) {
        ::utils::Logger::Get()->error("Unexpected error in ResponseHelper::Success: {}", e.what());
        res.status = 500;
        json error = {
            {"code", -1},
            {"msg", "Internal server error"}
        };
        try {
            res.set_content(error.dump(), "application/json");
        } catch (...) {
            res.set_content("{\"code\":-1,\"msg\":\"Internal server error\"}", "application/json");
        }
    }
}

void ResponseHelper::Error(httplib::Response& res, int code, const std::string& msg, int status) {
    json error = {
        {"code", code},
        {"msg", msg}
    };
    res.status = status;
    res.set_content(error.dump(), "application/json");
}

void ResponseHelper::Error(httplib::Response& res, const std::exception& e, int status) {
    Error(res, -1, e.what(), status);
}

void ResponseHelper::Error(httplib::Response& res, const gateway::GatewayException& e) {
    Error(res, e.code(), e.what(), e.http_status());
}

void ResponseHelper::BadRequest(httplib::Response& res, const std::string& msg) {
    Error(res, -1, msg, 400);
}

void ResponseHelper::NotFound(httplib::Response& res, const std::string& msg) {
    Error(res, -1, msg, 404);
}

void ResponseHelper::Conflict(httplib::Response& res, const std::string& msg) {
    Error(res, -2, msg, 409);
}

} // namespace utils
} // namespace api

