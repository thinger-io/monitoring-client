#pragma once

#include <chrono>
#include <ctime>   // localtime
#include <sstream> // stringstream
#include <iomanip> // put_time

namespace Date { // TODO: please, think. This file is kind of a mess. Treat it as objects that can undergo format changes maintaining the same reference, and not different objects and static methods each time

    static std::string now_iso8601(const char del = '-', const bool extended = false) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        //std::string format = "%Y"%m%d";
        //std::string format = std::string("%Y").append(&del);//"%m%d";
        std::string format =
            std::string("%Y").append(&del)+
            std::string("%m").append(&del)+
            std::string("%d");

        std::stringstream ss;
        if (extended)
            format =
                std::string("%Y").append(&del)+
                std::string("%m").append(&del)+
                "%dT%H%M%SZ";
            //ss << std::put_time(std::localtime(&in_time_t), "%Y".append(del)+"%m"+append(del)+"%d");
        ss << std::put_time(std::localtime(&in_time_t), format.c_str());
        // TODO: iso8601 extended standard
        //else

        return ss.str();
    }

    static std::string now_utc_iso8601(const char del = '-', const bool extended = false) {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        //std::string format = "%Y"%m%d";
        //std::string format = std::string("%Y").append(&del);//"%m%d";
        std::string format =
            std::string("%Y").append(&del)+
            std::string("%m").append(&del)+
            std::string("%d");

        std::stringstream ss;
        if (extended)
            format =
                std::string("%Y").append(&del)+
                std::string("%m").append(&del)+
                "%dT%H%M%SZ";
            //ss << std::put_time(std::localtime(&in_time_t), "%Y".append(del)+"%m"+append(del)+"%d");
        ss << std::put_time(std::gmtime(&in_time_t), format.c_str());
        // TODO: iso8601 extended standard
        //else

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
