#pragma once

#include <nlohmann/json.hpp>
#include <httplib.h>
#include <spdlog/spdlog.h>

namespace Platform::Utils::InfluxDB {

    std::string get_version() { // I could always execute a command in the docker container¿?
        httplib::Client cli("http://localhost:8086");

        auto res = cli.Get("/ping");

        if (res.error() != httplib::Error::Success) {
            spdlog::warn("[_INFLUX] Can't ping influxdb /ping endpoint");
            return "false"; // TODO: throw exception
        }

        std::string version = res->get_header_value("X-Influxdb-Version");

        return version;
    }

}
