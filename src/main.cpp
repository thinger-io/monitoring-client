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
  LOG_INFO("Starting thinger_monitor program");

  thinger::monitor::Config config;
  std::string transport;
  int verbosity_level = 1;

  namespace po = boost::program_options;

  // Define argumens
  po::options_description desc("options_description [options]");
  desc.add_options()
    ("help,h", "show this help")
    ("verbosity,v", po::value<int>(&verbosity_level)->default_value(1), "set verbosity level")
    ("token,t", po::value<std::string>(), "autoprovisioning token")
    ("ssl,k", po::value<bool>()->default_value(true), "secure connection")
    ("config,c", po::value<std::string>()->default_value("/etc/thinger_io/thinger_monitor.json"), "configuration file path")
    ("transport,p", po::value<std::string>(&transport)->default_value(""), "connection transport, i.e., 'websocket'");

  // Parse arguments
  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  // initialize logging library
#ifdef THINGER_LOG_SPDLOG
  spdlog_verbosity_level = verbosity_level;
  spdlog::default_logger()->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%^%l%$] [%s:%#] %v");
  spdlog::set_level(spdlog::level::trace);
#endif

  // Check options and call functions
  if (vm.count("ssl")) {
    config.set_ssl(vm["ssl"].as<bool>());
  }
  if (vm.count("config")) {
    config.set_path(vm["config"].as<std::string>());
  }
  // If the thinger token is passed we assume the device needs to be created
  if (vm.count("token")) {

    std::string token_str = vm["token"].as<std::string>();
    json token_json = JWT::get_payload(token_str);
    if (token_json.contains("svr"))
      config.set_url(token_json["svr"].get<std::string>());
    config.set_user(token_json["usr"].get<std::string>());

    config.set_device(); // checks if it's empty

    // If device exists and token has been passed change credentials
    if (Thinger::device_exists(token_str, config.get_user(), config.get_id(), config.get_url(), config.get_ssl())) {

      auto status = Thinger::update_device_credentials(token_str, config.get_user(), config.get_id(),
                                                       config.get_credentials(), config.get_url(), config.get_ssl());
      if (status == 200) {
        LOG_INFO("Credentials changed succesfully! Please run the program without the token");
        return 0;
      } else {
        LOG_WARNING("Could not change credentials, check the token and its permissions");
        return -1;
      }

    } else {
      // Create device when it does not exit and token has been passed
      auto status = Thinger::create_device(token_str, config.get_user(), config.get_id(), config.get_credentials(),
                                           config.get_name(), config.get_url(), config.get_ssl());
      if (status == 200) {
        LOG_INFO("Device created succesfully! Please run the program without the token");
        return 0;
      } else {
        LOG_WARNING("Could not create device, check the connection and make sure it doesn't already exist");
        return -1;
      }
    }

  }

  // run asio workers
  thinger::asio::workers.start();

  // create iotmp client
  iotmp::client client(transport);

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
  for (auto const &property: config.remote_properties) {
    client.property_stream(property.c_str(), true) = [&config, &property, &monitor](iotmp::input& in) {
      LOG_INFO("Received property (%s) update value", property);
      config.update(property, in["value"]);
      monitor.reload_configuration(property);
    };
  }

  // start client
  client.start();

  // wait for asio workers to complete (receive a signal)
 thinger::asio::workers.wait();

  return 0;

}