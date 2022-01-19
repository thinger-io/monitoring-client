#pragma once

#include <chrono>
#include <ctime>   // localtime
#include <sstream> // stringstream
#include <iomanip> // put_time

namespace Date {

    static std::string now_iso8601(const bool extended = false) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        std::stringstream ss;
        if (!extended)
            ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d");
        // TODO
        //else
        //    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%dT%X%z);

        return ss.str();
    }

    static std::string now_rfc5322() {

        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%a, %d %b %Y %T %z");

        return ss.str();
    }

    // TODO change this to a monotonic clock implementation. Using c++11?
    static unsigned long millis() {
        struct timeval te;
        gettimeofday(&te, NULL);
        unsigned long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000;
        return milliseconds;
    }

};
