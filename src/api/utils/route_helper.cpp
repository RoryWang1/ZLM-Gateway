#include "api/utils/route_helper.hpp"

namespace api {
namespace utils {

void RouteHelper::RegisterGet(httplib::Server* server,
                              const std::string& path,
                              std::function<void(const httplib::Request&, httplib::Response&)> handler) {
    if (server) {
        server->Get(path, handler);
    }
}

void RouteHelper::RegisterPost(httplib::Server* server,
                               const std::string& path,
                               std::function<void(const httplib::Request&, httplib::Response&)> handler) {
    if (server) {
        server->Post(path, handler);
    }
}

void RouteHelper::RegisterDelete(httplib::Server* server,
                                 const std::string& path,
                                 std::function<void(const httplib::Request&, httplib::Response&)> handler) {
    if (server) {
        server->Delete(path, handler);
    }
}

} // namespace utils
} // namespace api

