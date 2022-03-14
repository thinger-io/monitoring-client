#pragma once

#include <httplib.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace Thinger {

    bool device_exists(const std::string &token, const std::string &user, const std::string &device, const std::string &server = THINGER_SERVER, const bool secure = true) {
        // For on premise and private instances set secure to false with option -k
        std::string protocol = secure ? "https://" : "http://";
        httplib::Client cli(protocol+server);
        #if OPEN_SSL
          if (!secure) {
              cli.enable_server_certificate_verification(false);
          }
        #endif

        const httplib::Headers headers = {
            { "Authorization", "Bearer "+token}
        };

        auto res = cli.Get(("/v1/users/"+user+"/devices/"+device).c_str(), headers);

        return res->status == 200 ? true : false;
    }

    int update_device_credentials(const std::string &token, const std::string &user, const std::string &device, const std::string &credentials, const std::string &server = THINGER_SERVER, const bool secure = true) {
        std::string protocol = secure ? "https://" : "http://";
        httplib::Client cli(protocol+server);
        #if OPEN_SSL
          if (!secure) {
              cli.enable_server_certificate_verification(false);
          }
        #endif

        const httplib::Headers headers = {
            { "Authorization", "Bearer "+token}
        };

        json body;
        body["credentials"] = credentials;

        auto res = cli.Put(("/v1/users/"+user+"/devices/"+device).c_str(), headers, body.dump(), "application/json");

        return res->status;
    }

    int create_device(const std::string &token, const std::string &user, const std::string &device, const std::string &credentials, const std::string &name, const std::string &server = THINGER_SERVER, const bool secure = true) {
        // For on premise and private instances set secure to false with option -k
        std::string protocol = secure ? "https://" : "http://";
        httplib::Client cli(protocol+server);
        #if OPEN_SSL
          if (!secure) {
              cli.enable_server_certificate_verification(false);
          }
        #endif

        const httplib::Headers headers = {
            { "Authorization", "Bearer "+token}
        };

        json body;
        body["device"] = device;
        body["credentials"] = credentials;
        body["name"] = name;
        body["description"] = "Linux Monitoring autoprovision";
        body["type"] = "Generic";

        auto res = cli.Post(("/v1/users/"+user+"/devices").c_str(), headers, body.dump(), "application/json");

        return res->status;

    }

    int call_endpoint(const std::string& token, const std::string& user, const std::string& endpoint, const json& payload, const std::string& server = THINGER_SERVER, const bool secure = true) {

        std::string protocol = secure ? "https://" : "http://";
        httplib::Client cli(protocol+server);
        #if OPEN_SSL
          if (!secure) {
            cli.enable_server_certificate_verification(false);
          }
        #endif

        const httplib::Headers headers = {
            { "Authorization", "Bearer "+token}
        };

        auto res = cli.Post(("/v1/users/"+user+"/endpoints/"+endpoint+"/call").c_str(), headers, payload.dump(), "application/json");

        return res->status;

    }
};
