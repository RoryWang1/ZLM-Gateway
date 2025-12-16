#include "api/handlers/process_handler.hpp"
#include "api/utils/response_helper.hpp"
#include <iostream>

using json = nlohmann::json;

namespace api {
namespace handlers {

ProcessHandler::ProcessHandler(std::shared_ptr<process::ProcessManager> process_manager)
    : process_manager_(process_manager) {}

void ProcessHandler::HandleGetProcesses(const httplib::Request&, httplib::Response& res) {
    auto processes = process_manager_->GetAllProcesses();
    
    json process_list = json::array();
    for (const auto& info : processes) {
        json p;
        p["id"] = info.process_id;
        p["name"] = info.name;
        p["pid"] = info.pid;
        p["status"] = static_cast<int>(info.status);
        p["status_str"] = info.status == process::ProcessStatus::Running ? "running" : 
                         (info.status == process::ProcessStatus::Starting ? "starting" : "stopped");
        p["restart_count"] = info.restart_count;
        p["target_app"] = info.target_app;
        p["target_stream"] = info.target_stream;
        p["source_url"] = info.source_url;
        p["cpu_usage"] = info.cpu_usage;
        p["memory_usage"] = info.memory_usage;
        process_list.push_back(p);
    }
    
    json response;
    response["processes"] = process_list;
    response["count"] = process_list.size();
    
    utils::ResponseHelper::Success(res, response);
}

void ProcessHandler::HandleGetProcess(const httplib::Request& req, httplib::Response& res) {
    std::string process_id;
    if (req.path_params.count("id")) {
        process_id = req.path_params.at("id");
    } else if (req.matches.size() > 1) {
        process_id = req.matches[1].str();
    } else {
        utils::ResponseHelper::Error(res, -1, "Process ID not provided", 400);
        return;
    }

    auto info = process_manager_->GetProcessInfo(process_id);
    if (info.process_id.empty()) {
        utils::ResponseHelper::Error(res, -1, "Process not found", 404);
        return;
    }
    
    json p;
    p["id"] = info.process_id;
    p["name"] = info.name;
    p["pid"] = info.pid;
    p["status"] = static_cast<int>(info.status);
    p["command"] = info.command;
    p["restart_count"] = info.restart_count;
    p["target_app"] = info.target_app;
    p["target_stream"] = info.target_stream;
    p["source_url"] = info.source_url;
    p["cpu_usage"] = info.cpu_usage;
    p["memory_usage"] = info.memory_usage;
    p["log_file"] = info.log_file;
    
    utils::ResponseHelper::Success(res, p);
}

} // namespace handlers
} // namespace api
