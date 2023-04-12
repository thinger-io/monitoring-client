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

#include <thinger/thinger.h>
#include "thinger/client.h"

#include <fmt/format.h>
#include <boost/program_options.hpp>

#include <unistd.h>

#include "thinger/utils/jwt.h"
#include "thinger/utils/thinger.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <httplib.h>

using json = nlohmann::json;

constexpr int CONFIG_DELAY = 10;

#ifdef THINGER_LOG_SPDLOG
    int spdlog_verbosity_level;
#endif

int main(int argc, char *argv[]) {

    std::string program = argv[0];

    spdlog::set_level(spdlog::level::debug); // Set global log level to debug
    spdlog::info("Starting thinger_monitor program");

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

        // initialize logging library
#ifdef THINGER_LOG_SPDLOG
  //spdlog_verbosity_level = verbosity_level;
  spdlog_verbosity_level = 1;
  spdlog::default_logger()->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%^%l%$] [%s:%#] %v");
  spdlog::set_level(spdlog::level::trace);
#endif

    // TODO: change getopt for program options
    //po::options_description desc(fmt::format("Usage: %s [-c config_path] [-t token] [-u user] [-s server] [-k]", argv[0]));
    //desc.add_options()
    //  ("help,h", "show this help")
    //  ("config,c", po::value<std::string>(), 

    // If the thinger token is passed we assume the device needs to be created
    if (!thinger_token.empty()) {

        json token = JWT::get_payload(thinger_token);
        if (token.contains("svr"))
            config.set_url(token["svr"].get<std::string>());
        config.set_user(token["usr"].get<std::string>());

        config.set_device(); // checks if it's empty

        if (Thinger::device_exists(thinger_token, config.get_user(), config.get_id(), config.get_url(), config.get_ssl())) {

            auto status = Thinger::update_device_credentials(thinger_token, config.get_user(), config.get_id(), config.get_credentials(), config.get_url(), config.get_ssl());
            if (status == 200) {
                spdlog::info("Credentials changed succesfully! Please run the program without the token");
                return 0;
            } else {
                spdlog::warn("Could not change credentials, check the token and its permissions");
                return -1;
            }

        } else {

            // TODO: check if device already exists, if not create it
            auto status = Thinger::create_device(thinger_token, config.get_user(), config.get_id(), config.get_credentials(), config.get_name(), config.get_url(), config.get_ssl());
            if (status == 200) {
                spdlog::info("Device created succesfully! Please run the program without the token");
                return 0;
            } else {
                spdlog::warn("Could not create device, check the connection and make sure it doesn't already exist");
                return -1;
            }
        }

    }

    // run asio workers
    thinger::asio::workers.start();

    // create iotmp client
    iotmp::client client(""); // TODO: set transport parameter; default "", alternative websocket

    // set client credentials and host
    client.set_credentials(config.get_user(), config.get_id(), config.get_credentials());
    std::string url = config.get_url();
    client.set_host(url.c_str());

    // initialize client version extension
    iotmp::version version(client);

    // initialize terminal extension
    iotmp::terminal shell(client);

    // initialize proxy extension
    iotmp::proxy proxy(client);

    thinger::monitor::Client monitor(client, config);

    // Retrieve properties on connection and any update
    for ( auto const& property : config.remote_properties ) {
      client.property_stream(property.c_str(), true) = [&config, &property, &monitor](iotmp::input& in) {
        LOG_INFO("Received property (%s) update value", property);
        config.update(property, in["value"]);
        monitor.reload_configuration(property);
      };
    }

    // Reload configuration on authentication
    /*client.set_state_listener([&](iotmp::THINGER_STATE state) {
      switch(state) {
        case iotmp::THINGER_AUTHENTICATED:
          monitor.reload_configuration();
          break;
        default:
          break;
      }
    });*/

    // start client
    client.start();

    // wait for asio workers to complete (receive a signal)
    thinger::asio::workers.wait();

    return 0;

}