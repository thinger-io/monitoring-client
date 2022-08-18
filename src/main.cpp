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

#define DEBUG

#include <thinger.h>
#include "thinger/client.h"

#include <unistd.h>

#include "thinger/utils/jwt.h"
#include "thinger/utils/thinger.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

constexpr int CONFIG_DELAY = 10;

//const std::vector<std::string> properties = {"resources","backups","storage"}; // thinger device config properties

int main(int argc, char *argv[]) {

    std::string thinger_token;
    thinger::monitor::Config config;

    int opt;
    while ((opt = getopt(argc, argv, "u:t:c:s:k")) != -1) {
        switch (opt) {
            case 'c':
                config.set_path(optarg);
                break;
            case 't':
                thinger_token = optarg;
                break;
            case 'u':
                config.set_user(optarg);
                break;
            case 's':
                config.set_url(optarg);
                break;
            case 'k':
                config.set_ssl(false);
                break;
            default: /* '?' */
                fprintf(stderr, "Usage: %s [-c config_path] [-t token] [-u user] [-s server] [-k]\n",
                    argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // If the thinger token is passed we assume the device needs to be created
    if (!thinger_token.empty()) {

        json token = JWT::get_payload(thinger_token);
        if (token.contains("svr"))
            config.set_url(token["svr"].get<std::string>());
        config.set_user(token["usr"].get<std::string>());

        if (config.get_user().empty()) {
            config.set_device();

            if (Thinger::device_exists(thinger_token, config.get_user(), config.get_id(), config.get_url(), config.get_ssl())) {

                auto status = Thinger::update_device_credentials(thinger_token, config.get_user(), config.get_id(), config.get_credentials(), config.get_url(), config.get_ssl());
                if (status == 200) {
                    std::cout << "Credentials changed succesfully! Please run the program without the token" << std::endl;
                    return 0;
                } else {
                    std::cout << "Could not change credentials, check the token and its permissions" << std::endl;
                    return -1;
                }

            } else {

                // TODO: check if device already exists, if not create it
                auto status = Thinger::create_device(thinger_token, config.get_user(), config.get_id(), config.get_credentials(), config.get_name(), config.get_url(), config.get_ssl());
                if (status == 200) {
                    std::cout << "Device created succesfully! Please run the program without the token" << std::endl;
                    return 0;
                } else {
                    std::cout << "Could not create device, check the connection and make sure it doesn't already exist" << std::endl;
                    return -1;
                }
            }
        }
    }

    // Connect to thinger
    const std::string user = config.get_user();
    const std::string device_id = config.get_id();
    const std::string device_credentials = config.get_credentials();
    const std::string server = config.get_url();
    thinger_device thing(
        user.c_str(),
        device_id.c_str(),
        device_credentials.c_str(),
        server.c_str()
    );

    thinger::monitor::Client monitor(thing, config);

    // TODO: clean once get property returns empty message instead of hanging onto the connection
    // For each reconnection align settings between remote and local
    /*thing.set_state_listener([&](thinger_client::THINGER_STATE state) {
        switch(state) {
            case thinger_client::THINGER_AUTHENTICATED:
                pson data;
                thing.get_property("_monitor", data);
                if (!data.is_empty()) {
                    config.update_with_remote(data);
                } else {
                    //thing.handle()
                    pson c_data = config.in_pson();
                    thing.set_property("_monitor", c_data);
                }
                break;
        }
    });*/

   thing.handle();

    // On start Check if properties are online and update config file, or viceversa
    for (auto const& property : config.remote_properties) {

        pson data;
        thing.get_property(property.c_str(), data);
        if (!data.is_empty()) {
            config.update(property, data);
        } else {
            thing.handle();
            pson c_data = config.get(property); // in pson
            thing.set_property(property.c_str(), c_data);
        }
    }

    unsigned long delay = 0;
    while (true) {
        thing.handle();
        // After reconnection, if we've reached this far resources will exist in config
        unsigned long current_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (current_seconds >= (delay+CONFIG_DELAY)) {
            // Retrieve remote property
            bool updated = false;
            for (auto const& property : config.remote_properties) {
                pson r_data;
                thing.get_property(property.c_str(), r_data);
                updated = updated || config.update(property, r_data);

            }

            if (updated)
                 monitor.reload_configuration();

            delay = current_seconds;
        }

    }

}
