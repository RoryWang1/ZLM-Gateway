#include "api/utils/route_helper.hpp"

namespace api {
namespace utils {

void RouteHelper::RegisterGet(
    httplib::Server *server, const std::string &path,
    std::function<void(const httplib::Request &, httplib::Response &)>
        handler) {
  if (server) {
    server->Get(path.c_str(), handler);
  }
}

void RouteHelper::RegisterPost(
    httplib::Server *server, const std::string &path,
    std::function<void(const httplib::Request &, httplib::Response &)>
        handler) {
  if (server) {
    server->Post(path.c_str(), handler);
  }
}

void RouteHelper::RegisterDelete(
    httplib::Server *server, const std::string &path,
    std::function<void(const httplib::Request &, httplib::Response &)>
        handler) {
  if (server) {
    server->Delete(path.c_str(), handler);
  }
}

} // namespace utils
} // namespace api
