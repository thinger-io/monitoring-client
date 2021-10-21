#include <httplib.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

static int create_device(const std::string &token, const std::string &user, const std::string &device, const std::string &credentials, const std::string &server = THINGER_SERVER, const bool secure = true) {
    // TODO: disable certificate verification on on premise and private ip instances
    std::string protocol = secure ? "https://" : "http://";
    httplib::Client cli(protocol+server);
    if (!secure) {
        cli.enable_server_certificate_verification(false);
    }

    const httplib::Headers headers = {
        { "Authorization", "Bearer "+token}
    };

    json body;
    body["device"] = device;
    body["credentials"] = credentials;
    body["name"] = device+" autoprovision";
    body["description"] = "Linux Monitoring autoprovision";
    body["type"] = "Generic";

    auto res = cli.Post(("/v1/users/"+user+"/devices").c_str(), headers, body.dump(), "application/json");

    return res->status;

}
