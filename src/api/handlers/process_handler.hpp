#ifndef API_HANDLERS_PROCESS_HANDLER_HPP
#define API_HANDLERS_PROCESS_HANDLER_HPP

#include "process/process_manager.hpp"
#include <httplib.h>
#include <memory>
#include <nlohmann/json.hpp>

namespace api {
namespace handlers {

class ProcessHandler {
public:
    ProcessHandler(std::shared_ptr<process::ProcessManager> process_manager);

    void HandleGetProcesses(const httplib::Request& req, httplib::Response& res);
    void HandleGetProcess(const httplib::Request& req, httplib::Response& res);

private:
   std::shared_ptr<process::ProcessManager> process_manager_;
};

} // namespace handlers
} // namespace api

#endif // API_HANDLERS_PROCESS_HANDLER_HPP
