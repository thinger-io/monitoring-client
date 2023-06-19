
#include <httplib.h>

namespace thinger::monitor::network {

    struct interface {
        std::string name;
        std::string internal_ip;
        std::array<std::array<unsigned long long int, 2>, 3> total_transfer; // before, after; b incoming, b outgoing, 3-> ts
        std::array<unsigned long long, 4> total_packets; // incoming (total, dropped), outgoing (total, dropped)
    };

    std::string getPublicIPAddress() {
        httplib::Client cli("https://ifconfig.me");
        auto res = cli.Get("/ip");
        if ( res.error() == httplib::Error::SSLServerVerification ) {
            httplib::Client cli("http://ifconfig.me");
            res = cli.Get("/ip");
        }
        return res->body;
    }

    std::string getIPAddress(const std::string_view& interface){
        std::string ipAddress="Unable to get IP Address";
        struct ifaddrs *interfaces = nullptr;
        struct ifaddrs *temp_addr = nullptr;
        int success = 0;
        // retrieve the current interfaces - returns 0 on success
        success = getifaddrs(&interfaces);
        if (success == 0) {
            // Loop through linked list of interfaces
            temp_addr = interfaces;
            while(temp_addr != nullptr) {
                if(temp_addr->ifa_addr != nullptr
                  && temp_addr->ifa_addr->sa_family == AF_INET
                  && temp_addr->ifa_name == interface)
                {
                    // Check if interface is the default interface
                    ipAddress=inet_ntoa(((struct sockaddr_in*)temp_addr->ifa_addr)->sin_addr);
                }
                temp_addr = temp_addr->ifa_next;
            }
        }
        // Free memory
        freeifaddrs(interfaces);
        return ipAddress;
    }

    void retrieve_ifc_stats(std::vector<interface>& interfaces) {
        for (auto & ifc : interfaces) {

            std::ifstream netinfo ("/proc/net/dev", std::ifstream::in);
            std::string line;
            std::string null;

            while(netinfo >> line) {
                if (line == ifc.name+":") {
                    netinfo >> ifc.total_transfer[0][1]; //first bytes inc
                    netinfo >> ifc.total_packets[0]; // total packets inc
                    netinfo >> null >> ifc.total_packets[1]; // drop packets inc
                    netinfo >> null >> null >> null >> null >> ifc.total_transfer[1][1]; // total bytes out
                    netinfo >> ifc.total_packets[2]; // total packets out
                    netinfo >> null >> ifc.total_packets[3]; // drop packets out

                    ifc.total_transfer[2][1] = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count();

                    break;
                }
            }
        }
    }

}

namespace thinger::monitor::io {

    struct drive {
        std::string name;
        std::array<std::array<unsigned long long int, 4>, 2> total_io; // before, after; sectors read, sectors writte, io tics, ts
    };

    void retrieve_dv_stats(std::vector<drive>& drives) {
        for(auto & dv : drives) {

            std::ifstream dvinfo ("/sys/block/"+dv.name+"/stat", std::ifstream::in);
            std::string null;

            dvinfo >> null >> null >> dv.total_io[1][0]; // sectors read [0]
            dvinfo >> null >> null >> null >> dv.total_io[1][1]; // sectors written [1]
            dvinfo >> null >> null >> dv.total_io[1][2]; // io ticks -> time spent in io [2]

            dv.total_io[1][3] = std::chrono::duration_cast<std::chrono::milliseconds>( // millis [3]
              std::chrono::system_clock::now().time_since_epoch()).count();
        }
    }

}

namespace thinger::monitor::storage {

    struct filesystem {
        std::string path;
        std::filesystem::space_info space_info;
    };

    void retrieve_fs_stats(std::vector<filesystem>& filesystems) {
        for (auto & fs : filesystems) {
            fs.space_info = std::filesystem::space(fs.path);
        }
    }

}

namespace thinger::monitor::cpu {

    void retrieve_cpu_cores(unsigned int& cores) {
        cores = std::thread::hardware_concurrency();
    }

    template <size_t N>
    void retrieve_cpu_loads(std::array<float, N>& loads) {
        std::ifstream loadinfo ("/proc/loadavg", std::ifstream::in);
        for (auto i = 0; i < 3; i++) {
            loadinfo >> loads[i];
        }
    }

    template <size_t N>
    void retrieve_cpu_usage(float& usage, std::array<float, N> const& loads, unsigned int const& cores) {
        usage = loads[0] * 100 / cores;
    }

    void retrieve_cpu_procs(unsigned int& procs) {
        std::string path = "/proc";
        procs = 0;
        for (const auto & entry : std::filesystem::directory_iterator(path)) {
            if (entry.is_directory() && isdigit(entry.path().u8string().back()))  {
                procs++;
            }
        }
    }

}

namespace thinger::monitor::memory {

    void retrieve_ram(unsigned long& total, unsigned long& free, unsigned long& swp_total, unsigned long& swp_free) {

        std::ifstream raminfo ("/proc/meminfo", std::ifstream::in);
        std::string line;

        while(raminfo >> line) {
            if (line == "MemTotal:") {
                raminfo >> total;
            } else if (line == "MemAvailable:") {
                raminfo >> free;
            } else if (line == "SwapTotal:") {
                raminfo >> swp_total;
            } else if (line == "SwapFree:") {
                raminfo >> swp_free;
                // From /proc/meminfo order once we reach available we may stop reading
                break;
            }
        }
    }
}

namespace thinger::monitor::system {

    void retrieve_hostname(std::string& hostname) {
        std::ifstream hostinfo ("/etc/hostname", std::ifstream::in);
        hostinfo >> hostname;
    }

    void retrieve_os_version(std::string& os_version) {
        std::ifstream osinfo ("/etc/os-release", std::ifstream::in);
        std::string line;

        while(std::getline(osinfo,line)) {
            if (line.find("PRETTY_NAME") != std::string::npos) {
                // get text in between "
                size_t first_del = line.find('"');
                size_t last_del = line.find_last_of('"');
                os_version = line.substr(first_del +1, last_del - first_del -1);
            }
        }
    }

    void retrieve_kernel_version(std::string& kernel_version) {
        std::ifstream kernelinfo ("/proc/version", std::ifstream::in);
        std::string version;

        kernelinfo >> kernel_version;
        kernelinfo >> version; // skipping second word
        kernelinfo >> version;

        kernel_version = kernel_version + " " + version;
    }

    void retrieve_uptime(std::string& uptime) {
        //std::chrono::milliseconds uptime_millis(0u);
        double uptime_seconds;
        if (std::ifstream("/proc/uptime", std::ios::in) >> uptime_seconds) {
            int days = (int)uptime_seconds / (60*60*24);
            int hours = ((int)uptime_seconds % (((days > 0) ? days : 1)*60*60*24)) / (60*60);
            int minutes = (int)uptime_seconds % (((days > 0) ? days : 1)*60*60*24) % (((hours > 0) ? hours: 1)*60*60) / 60;
            uptime =
              ((days > 0) ? std::to_string(days)+((days == 1) ? " day, ":" days, ") : "") +
              ((hours > 0) ? std::to_string(hours)+((hours == 1) ? " hour, ":" hours, ") : "") +
              std::to_string(minutes)+((minutes == 1) ? " minute":" minutes");
        }
    }
}

namespace thinger::monitor::platform {

    void getConsoleVersion(std::string& console_version) {
        httplib::Client cli("http://127.0.0.1");
        auto res = cli.Get("/v1/server/version");
        if (res.error() != httplib::Error::Success) {
            console_version = "Could not retrieve";
        } else {
            auto res_json = nlohmann::json::parse(res->body);
            console_version = res_json["version"].get<std::string>();
        }
  }

}
