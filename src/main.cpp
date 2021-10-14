// The MIT License (MIT)
//
// Copyright (c) 2015 THINGER LTD
// Author: alvarolb@gmail.com (Alvaro Luis Bustamante)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#if OPEN_SSL
  #define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#define THINGER_SERVER "192.168.1.16"

#include <thinger.h>
#include "thinger/thinger_monitor_client.h"

#include <httplib.h>
#include <unistd.h>

#define CONFIG_DELAY 10

//const std::vector<std::string> interfaces = {"eth0"};
//const std::vector<std::string> filesystems = {"/"};
//const std::vector<std::string> drives = {"xvda"};

int create_device(std::string token, std::string user, std::string device, std::string credentials) {
    // TODO: disable certificate verification on on premise and private ip instances
    httplib::Client cli("http://"+std::string(THINGER_SERVER));
    cli.enable_server_certificate_verification(false);

    const httplib::Headers headers = {
        { "Authorization", "Bearer "+token}
    };

    Json::Value body;
    body["device"] = device;
    body["credentials"] = credentials;
    body["name"] = device+" autoprovision";
    body["description"] = "Linux Monitoring autoprovision";
    body["type"] = "Generic";

    Json::StreamWriterBuilder wbuilder;
    const std::string body_json = Json::writeString(wbuilder, body);

    auto res = cli.Post(("/v1/users/"+user+"/devices").c_str(), headers, body_json, "application/json");

    return res->status;
}

int main(int argc, char *argv[]) {

    std::string thinger_token;
    ThingerMonitorConfig config;

    int opt;
    while ((opt = getopt(argc, argv, "u:t:c:")) != -1) {
        switch (opt) {
            case 'c':
                config.set_config_path(optarg);
                break;
            case 't':
                thinger_token = optarg;
                break;
            case 'u':
                config.set_user(optarg);
                break;
            //default: /* '?' */
            //    fprintf(stderr, "Usage: %s [-t nsecs] [-n] name\n",
            //        argv[0]);
            //    exit(EXIT_FAILURE);
        }
    }

    // If the thinger token is passed we assume the device (TODO: and bucket) needs to be created
    if (!thinger_token.empty() && config.has_user()) {
        config.set_device();

        auto status = create_device(thinger_token, config.get_user(), config.get_device_id(), config.get_device_credentials());

    }

    // Connect to thinger
    const std::string user = config.get_user();
    const std::string device_id = config.get_device_id();
    const std::string device_credentials = config.get_device_credentials();
    thinger_device thing(
        user.c_str(),
        device_id.c_str(),
        device_credentials.c_str()
    );

    ThingerMonitor monitor(thing, config);

    // Connect once and update property
    // Align settings between remote and local
    thing.handle();

    pson data;
    thing.get_property("_monitor", data);
    if (!data.is_empty()) {
        config.update_with_remote(data);
    } else {
        pson c_data = config.in_pson();
        thing.set_property("_monitor", c_data);
    }

    unsigned long delay = 0;
    while (true) {
        thing.handle();

        // After reconnection, if we've reached this far resources will exist in config
        unsigned long current_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (current_seconds >= (delay+CONFIG_DELAY)) {

            // Retrieve remote property
            pson r_data;
            thing.get_property("_monitor", r_data);
            bool updated = config.update_with_remote(r_data);

            if (updated) {
                monitor.reload_configuration();
            }

            delay = current_seconds;
        }

    }

    return 0;
}
