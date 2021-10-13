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
#define DEBUG

#if OPEN_SSL
  #define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

//#ifndef THINGER_SERVER
//    #define THINGER_SERVER "iot.thinger.io"
//#endif

#define THINGER_SERVER "192.168.1.16"

#include <thinger.h>
#include "thinger/thinger_monitor_client.h"

#include <jsoncpp/json/json.h>
#include <httplib.h>

#include <unistd.h>
#include <fstream>
#include <filesystem>
#include <random>


using std::filesystem::current_path;

//const std::vector<std::string> interfaces = {"wlan0"}; // TODO: change to eth0 in AWS Ubuntu
//const std::vector<std::string> filesystems = {"/"}; // In AWS we are only interested in /
//const std::vector<std::string> drives = {"nvme0n1"}; // In AWS we are only interested in xvda

void merge_json(Json::Value& a, Json::Value& b) {
    if (!a.isObject() || !b.isObject()) return;

    for (const auto& key : b.getMemberNames()) {
        if (a[key].isObject()) {
            merge_json(a[key], b[key]);
        } else {
            a[key] = b[key];
        }
    }
}

std::string generate_credentials(std::size_t length) {

    std::string CHARACTERS = "0123456789abcdefghijklmnopqrstuvwxyz!@#$%^&*()ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    std::random_device random_device;
    std::mt19937 generator(random_device());
    std::uniform_int_distribution<> distribution(0, CHARACTERS.size() - 1);

    std::string random_string;

    for (std::size_t i = 0; i < length; ++i)
    {
        random_string += CHARACTERS[distribution(generator)];
    }

    return random_string;

}

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

void save_configuration(std::string config_path, std::string user, std::string device, std::string credentials) {

    Json::Value config;
    config["user"] = user;
    config["device"]["id"] = device;
    config["device"]["credentials"] = credentials;


    Json::StreamWriterBuilder wbuilder;
    wbuilder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(wbuilder.newStreamWriter());
    std::ofstream config_file(config_path);
    writer->write(config, &config_file);
}

int main(int argc, char *argv[]) {

    std::string thinger_token;
    Json::Value config;

    std::string config_file_path = current_path().string()+"/../config/app.json"; // TODO: change path
    //std::string config_file_path = strcat(current_path().u8string(),"/../config/app.json"); // TODO: change path

    int opt;
    while ((opt = getopt(argc, argv, "u:t:c:")) != -1) {
        switch (opt) {
            case 'c':
                config_file_path = optarg;
                break;
            case 't':
                thinger_token = optarg;
                break;
            case 'u':
                config["user"] = optarg;
                break;
            //default: /* '?' */
            //    fprintf(stderr, "Usage: %s [-t nsecs] [-n] name\n",
            //        argv[0]);
            //    exit(EXIT_FAILURE);
        }
    }

    std::filesystem::path f(config_file_path);
    // Read config file
    if (std::filesystem::exists(f)) {
        std::ifstream config_file(config_file_path, std::ifstream::binary);
        if (config.empty()) {
            config_file >> config;
        } else {
            Json::Value config_tmp;
            config_file >> config_tmp;
            merge_json(config, config_tmp); // TODO: test if both objects have same member
        }
    }

    // If the thinger token is passed we assume the device needs to be created
    if (!thinger_token.empty() && config.isMember("user")) {
        // Create device and save configuration if succeeds
        // Check if device name exists, if not set it to hostname
        if (!config.isMember("device") || !config["device"].isMember("id")) {
            std::ifstream hostinfo ("/etc/hostname", std::ifstream::in);
            std::string hostname;
            hostinfo >> hostname;
            config["device"]["id"] = hostname;
        }
        if (!config["device"].isMember("credentials")) {
            config["device"]["credentials"] = generate_credentials(16);
        }

        auto status = create_device(thinger_token, config["user"].asString(), config["device"]["id"].asString(), config["device"]["credentials"].asString());
        if (status == 200) {
            save_configuration(config_file_path, config["user"].asString(), config["device"]["id"].asString(), config["device"]["credentials"].asString());
        }
    }

    // Get resources
    bool defaults;
    std::vector<std::string> interfaces;
    std::vector<std::string> filesystems;
    std::vector<std::string> drives;
    if (config.isMember("resources")) {
        if (config["resources"].isMember("interfaces")) {
            for (auto ifc : config["resources"]["interfaces"]) {
                interfaces.push_back(ifc.asString());
            }
        }
        if (config["resources"].isMember("filesystems")) {
            for (auto fs : config["resources"]["filesystems"]) {
                filesystems.push_back(fs.asString());
            }
        }
        if (config["resources"].isMember("drives")) {
            for (auto dv : config["resources"]["drives"]) {
                drives.push_back(dv.asString());
            }
        }
        defaults = (config["resources"].isMember("defaults")) ? config["resources"]["defaults"].asBool() : true;
    }

    // Connect to thinger
    const std::string user = config["user"].asString();
    const std::string device_id = config["device"]["id"].asString();
    const std::string device_credentials = config["device"]["credentials"].asString();
    thinger_device thing(
        user.c_str(),
        device_id.c_str(),
        device_credentials.c_str()
    );

    ThingerMonitor* monitor = new ThingerMonitor(thing, filesystems, drives, interfaces, defaults);

    // Connect once and update property
    thing.handle();

    pson data;
    for (auto rs : config["resources"].getMemberNames()) {
        if (config["resources"][rs].isBool()) data[rs.c_str()] = config["resources"][rs].asBool();
        else {
            pson_array& array = data[rs.c_str()];
            for (auto rs_val : config["resources"][rs]) {
                array.add(rs_val.asString());
            }
        }
    }
    thing.set_property("_monitor", data);


    unsigned long every30s = 0;
    while (true) {
        thing.handle();

        // After reconnection, if we've reached this far resources will exist in config
/*        unsigned long current_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (current_seconds >= (every30s+10)) {
            pson data;
            thing.get_property("_monitor", data);
            std::vector<std::string> resources = {"defaults", "interfaces", "filesystems", "drives"};
            Json::Value remote_config;
            for (auto rs : resources) {
                if (data[rs.c_str()].is_empty()) continue;

                if (data[rs.c_str()].is_boolean()) remote_config[rs] = data[rs.c_str()];
                if (data[rs.c_str()].is_array()) {
                    pson_array& array = data[rs.c_str()];
                    pson_container<pson>::iterator it = array.begin();
                    while (it.valid()) {
                        std::string string = it.item();
                        std::cout << string << std::endl;
                        remote_config[rs].append(string);
                        it.next();
                    }
                }
            }

            // Check differences and update config file
            if (remote_config.compare(config["resources"]) != 0) {
                config["resources"] = remote_config;
                Json::StreamWriterBuilder wbuilder;
                wbuilder["indentation"] = "  ";
                std::unique_ptr<Json::StreamWriter> writer(wbuilder.newStreamWriter());
                std::ofstream config_file(config_file_path);
                writer->write(config, &config_file);

                // Recall monitor
                interfaces.clear();
                filesystems.clear();
                drives.clear();
                if (config.isMember("resources")) {
                    if (config["resources"].isMember("interfaces")) {
                        for (auto ifc : config["resources"]["interfaces"]) {
                            interfaces.push_back(ifc.asString());
                        }
                    }
                    if (config["resources"].isMember("filesystems")) {
                        for (auto fs : config["resources"]["filesystems"]) {
                            filesystems.push_back(fs.asString());
                        }
                    }
                    if (config["resources"].isMember("drives")) {
                        for (auto dv : config["resources"]["drives"]) {
                            drives.push_back(dv.asString());
                        }
                    }
                    defaults = (config["resources"].isMember("defaults")) ? config["resources"]["defaults"].asBool() : true;
                }

                delete monitor;
                monitor = new ThingerMonitor(thing, filesystems, drives, interfaces, defaults);
            }

            every30s = current_seconds;
        }
*/
    }

    delete monitor;

    return 0;
}
