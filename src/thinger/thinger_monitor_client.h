#include "thinger_monitor_config.h"

#include <thinger_client.h>

#include <httplib.h>

#include <thread>
#include <chrono>
#include <fstream>
#include <ifaddrs.h>
#include <filesystem>
#include <arpa/inet.h>
#include <linux/kernel.h>
#include <unistd.h>

#include "utils/thinger.h"

#include "system/platform/backup.h"
#include "system/platform/restore.h"

namespace fs = std::filesystem;

#if OPEN_SSL
  #define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#define STR_(x) #x
#define STR(x) STR_(x)
#ifdef BUILD_VERSION
  #define VERSION STR(BUILD_VERSION)
#else
  #define VERSION "Not set"
#endif

#define SECTOR_SIZE 512

// Conversion constants. //
const long minute = 60;
const long hour = minute * 60;
const long day = hour * 24;

const double btokb = 1024;
const double kbtogb = btokb * 1024;
const double btomb = kbtogb;
const double btogb = kbtogb * 1024;

class ThingerMonitor {

public:

    ThingerMonitor(thinger_client& client, ThingerMonitorConfig& config) :
        client_(client),
        monitor_(client["monitor"]),
        cmd_(client["cmd"]),
        reboot_(client["reboot"]),
        update_(client["update"]),
        update_distro_(client["update_distro"]),
        backup_(client["backup"]),
        restore_(client["restore"]),
        config_(config)
    {

            reload_configuration();

            // Executed only once
            retrieve_hostname();
            retrieve_os_version();
            retrieve_kernel_version();
            retrieve_cpu_cores();

            cmd_ = [this](pson& in, pson& out) {
                out["output"] = cmd(in["input"]);
            };

            if (geteuid() == 0) { // is_root
                reboot_ << [this](pson& in) { // needs declaration of input for dashboard button
                    if (in)
                        reboot();
                };

                update_ << [this](pson& in) { // needs declaration of input for dashboard button
                    if (in)
                        update();
                };

                update_distro_ << [this](pson& in) { // needs declaration of input for dashboard button
                    if (in)
                        update_distro();
                };
           }

            backup_ = [this](pson& in, pson& out) {
                std::string endpoint = in["endpoint"];
                out["status"] = "";
                if (in["backup"]) {
                    in["backup"] = false;

                    ThingerBackup backup(config_, hostname);
                    std::cout << std::fixed << Date::millis()/1000.0 << " ";
                    std::cout << "[_BACKUP] Creating backup" << std::endl;
                    backup.create_backup();
                    std::cout << std::fixed << Date::millis()/1000.0 << " ";
                    std::cout << "[_BACKUP] Uploading backup" << std::endl;
                    out["status"] = backup.upload_backup();
                    std::cout << std::fixed << Date::millis()/1000.0 << " ";
                    std::cout << "[_BACKUP] Cleaning backup temporary files" << std::endl;
                    backup.clean_backup();

                    if (!endpoint.empty()) {
                        json payload;
                        payload["device"] = config_.get_device_id();
                        payload["hostname"] = hostname;
                        Thinger::call_endpoint(config_.get_backups_endpoints_token(), config_.get_user(), endpoint, payload, config_.get_server_url(), config_.get_server_secure());
                    }
                }
                //out["output"] = in["tag"];
            };

            restore_ = [this](pson& in, pson& out) {
                std::string tag = in["tag"];
                std::string endpoint = in["endpoint"];
                out["status"] = "";
                if (!tag.empty()) {
                    ThingerRestore restore(config_, hostname, tag);
                    std::cout << std::fixed << Date::millis()/1000.0 << " ";
                    std::cout << "[___RSTR] Downloading backup" << std::endl;
                    restore.download_backup();
                    std::cout << std::fixed << Date::millis()/1000.0 << " ";
                    std::cout << "[___RSTR] Restoring backup" << std::endl;
                    restore.restore_backup();
                    std::cout << std::fixed << Date::millis()/1000.0 << " ";
                    std::cout << "[___RSTR] Cleaning backup temporary files" << std::endl;
                    restore.clean_backup();
                    if (!endpoint.empty()) {
                        json payload;
                        payload["device"] = config_.get_device_id();
                        payload["hostname"] = hostname;
                        Thinger::call_endpoint(config_.get_backups_endpoints_token(), config_.get_user(), endpoint, payload, config_.get_server_url(), config_.get_server_secure());
                    }
                }
            };

            monitor_ >> [this](pson& out) {

                unsigned long current_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                // Set values at defined intervals
                if (current_seconds >= (every5s + 5)) {
                    retrieve_cpu_loads(); // load and usage is updated every 5s by sysinfo
                    retrieve_cpu_usage();
                    every5s = current_seconds;
                }

                if (current_seconds >= (every1m + 60)) {
                    retrieve_uptime();
                    retrieve_cpu_procs();
                    retrieve_fs_stats();
                    every1m = current_seconds;
                }

                if (current_seconds >= (every5m + 60*5)) {
                    retrieve_restart_status();
                    retrieve_updates();
                    every5m = current_seconds;
                }

                if (current_seconds >= (every1d + 60*60*24)) {
                    getPublicIPAddress();
                    every1d = current_seconds;
                }

                // Storage
                for (auto & fs : filesystems_) {
                    std::string name = fs.path;
                    if ( config_.get_defaults() && &fs == &filesystems_.front()) {
                        name = "default";
                    }
                    out[("st_"+name+"_capacity").c_str()] = fs.space_info.capacity / btogb;
                    out[("st_"+name+"_used").c_str()] = (fs.space_info.capacity - fs.space_info.free) / btogb;
                    out[("st_"+name+"_free").c_str()] = fs.space_info.free / btogb;
                    out[("st_"+name+"_usage").c_str()] = ((fs.space_info.capacity - fs.space_info.free)*100) / fs.space_info.capacity; // usaged based on time of io spend doing io operations
                }

                // IO
                retrieve_dv_stats();
                for (auto & dv : drives_) {
                    std::string name = dv.name;
                    if ( config_.get_defaults() && &dv == &drives_.front()) {
                        name = "default";
                    }

                    float speed_reading = (((float)(dv.total_io[1][0] - dv.total_io[0][0]) * SECTOR_SIZE) /
                        (float)(dv.total_io[1][3] - dv.total_io[0][3]))*1000;
                    float speed_writing = (((float)(dv.total_io[1][1] - dv.total_io[0][1]) * SECTOR_SIZE) /
                        (float)(dv.total_io[1][3] - dv.total_io[0][3]))*1000;
                    float usage = (float)(dv.total_io[1][2] - dv.total_io[0][2]) / (float)(dv.total_io[1][3] - dv.total_io[0][3]);

                    out[("dv_"+name+"_speed_read").c_str()] = speed_reading / btokb;
                    out[("dv_"+name+"_speed_written").c_str()] = speed_writing / btokb;
                    out[("dv_"+name+"_usage").c_str()] = usage < 1 ? usage * 100 : 100;

                    // flip matrix for speed and usage calculations
                    for (int z = 0; z < 4; z++) {
                        dv.total_io[0][z] = dv.total_io[1][z];
                    }
                }

                // Network
                retrieve_ifc_stats();
                for (auto & ifc : interfaces_) {
                    std::string name = ifc.name;
                    if ( config_.get_defaults() && &ifc == &interfaces_.front()) {
                        name = "default";
                    }

                    out[("nw_"+name+"_internal_ip").c_str()] = ifc.internal_ip;

                    out[("nw_"+name+"_total_incoming").c_str()] = ifc.total_transfer[1][0] / btogb;
                    out[("nw_"+name+"_total_outgoing").c_str()] = ifc.total_transfer[1][1] / btogb;
                    out[("nw_"+name+"_packetloss_incoming").c_str()] = ifc.total_packets[1];
                    out[("nw_"+name+"_packetloss_outgoing").c_str()] = ifc.total_packets[3];

                    // speeds in B/s
                    float speed_incoming = ((float)(ifc.total_transfer[1][0] - ifc.total_transfer[0][0]) /
                        (float)(ifc.total_transfer[1][2] - ifc.total_transfer[0][2]))*1000;
                    float speed_outgoing = ((float)(ifc.total_transfer[1][1] - ifc.total_transfer[0][1]) /
                        (float)(ifc.total_transfer[1][2] - ifc.total_transfer[0][2]))*1000;

                    out[("nw_"+name+"_speed_incoming").c_str()] = speed_incoming * 8 / btokb;
                    out[("nw_"+name+"_speed_outgoing").c_str()] = speed_outgoing * 8 / btokb;

                    // flip matrix for speed and usage calculations
                    for (int j = 0; j < 3; j++) {
                        ifc.total_transfer[0][j] = ifc.total_transfer[1][j];
                    }
                }
                out["nw_public_ip"] = public_ip;

                // RAM
                retrieve_ram();
                out["ram_total"] = (double)ram_total / kbtogb;
                out["ram_available"] = (double)ram_available / kbtogb;
                out["ram_used"] = (double)(ram_total - ram_available) / kbtogb;
                out["ram_usage"] = (double)((ram_total - ram_available) * 100) / ram_total;
                out["ram_swaptotal"] = (double)ram_swaptotal / kbtogb;
                out["ram_swapfree"] = (double)ram_swaptotal / kbtogb;
                out["ram_swapusage"] = (ram_swaptotal == 0) ? 0 : (double)((ram_swaptotal - ram_swapfree) *100) / ram_swaptotal;

                // CPU
                out["cpu_cores"] = cpu_cores;
                out["cpu_load_1m"] = cpu_loads[0];
                out["cpu_load_5m"] = cpu_loads[1];
                out["cpu_load_15m"] = cpu_loads[2];
                out["cpu_usage"] = cpu_usage;
                out["cpu_procs"] = cpu_procs;

                // System information
                out["si_uptime"] = uptime;
                out["si_hostname"] = hostname;
                out["si_os_version"] = os_version;
                out["si_kernel_version"] = kernel_version;
                out["si_normal_updates"] = normal_updates;
                out["si_security_updates"] = security_updates;
                out["si_restart"] = system_restart;
                out["si_sw_version"] = VERSION;

            };
    }

    virtual ~ThingerMonitor(){
    }

    void reload_configuration() {
        interfaces_.clear();
        filesystems_.clear();
        drives_.clear();

        for (auto fs_path : config_.get_filesystems()) {
            filesystem fs;
            fs.path = fs_path;
            filesystems_.push_back(fs);
        }

        for (auto dv_name : config_.get_drives()) {
            drive dv;
            dv.name = dv_name;
            drives_.push_back(dv);
        }

        for (auto ifc_name : config_.get_interfaces()) {
            interface ifc;
            ifc.name = ifc_name;
            ifc.internal_ip = getIPAddress(ifc_name);
            interfaces_.push_back(ifc);
        }

    }

protected:
    // thinger resources
    thinger_client& client_;
    thinger::thinger_resource& monitor_;
    thinger::thinger_resource& cmd_;
    thinger::thinger_resource& reboot_;
    thinger::thinger_resource& update_;
    thinger::thinger_resource& update_distro_;
    thinger::thinger_resource& backup_;
    thinger::thinger_resource& restore_;

    ThingerMonitorConfig& config_;

    // network
    struct interface {
        std::string name;
        std::string internal_ip;
        unsigned long long int total_transfer[2][3]; // before, after; b incoming, b outgoing, 3-> ts
        unsigned long long total_packets[4]; // incoming (total, dropped), outgoing (total, dropped)
    };
    std::vector<interface> interfaces_;
    std::string public_ip;

    // storage
    struct filesystem {
        std::string path;
        std::filesystem::space_info space_info;
    };
    std::vector<filesystem> filesystems_;

    // io
    struct drive {
        std::string name;
        unsigned long long int total_io[2][4]; // before, after; sectors read, sectors writte, io tics, ts
    };
    std::vector<drive> drives_;

    // system info
    std::string hostname;
    std::string os_version;
    std::string kernel_version;
    std::string uptime;
    unsigned short normal_updates = 0, security_updates = 0;
    bool system_restart = false;

    // CPU
    const float f_load = 1.f / (1 << SI_LOAD_SHIFT);
    float cpu_loads[3]; // 1, 5 and 15 mins loads
    float cpu_usage;
    unsigned short cpu_cores;
    unsigned int cpu_procs;

    // ram
    unsigned long ram_total, ram_available, ram_swaptotal, ram_swapfree;

    // timing variables
    unsigned long every5s = 0;
    unsigned long every1m = 0;
    unsigned long every5m = 0;
    unsigned long every1d = 0;

    // -- SYSTEM INFO -- //
    void retrieve_updates() {
        // We will use default ubuntu server notifications
        fs::path f("/var/lib/update-notifier/updates-available");
        if (fs::exists(f)) {
            std::ifstream updatesinfo ("/var/lib/update-notifier/updates-available", std::ifstream::in);
            std::string line;
            updatesinfo >> normal_updates;
            if(getline(updatesinfo, line)) {
                updatesinfo >> security_updates;
            } else {
                security_updates = 0;
            }
        }
    }

    void retrieve_restart_status() {
        fs::path f("/var/run/reboot-required");
        if (fs::exists(f)) {
            system_restart = true;
        } else {
            system_restart = false;
        }
    }

    std::string cmd(const char *in) {
        std::array<char, 128> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(in, "r"), pclose);
        if (!pipe) {
            return "Failed to run command\n";
        }

        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        return result;
    }

    void reboot() {
        system("sudo reboot");
    }

    void update() {
        // System upgrade. By default it does not overwrite config files if a package has a newer version
        system("sudo apt -y update && sudo DEBIAN_FRONTEND=noninteractive DEBIAN_PRIORITY=critical UCF_FORCE_CONFFOLD=1 apt -qq -y -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold upgrade");
    }

    void update_distro() {
        // Full unattended distro upgrade
        system("sudo apt -y update && sudo do-release-upgrade -f DistUpgradeViewNonInteractive");
    }

private:

    std::string getIPAddress(std::string interface){
        std::string ipAddress="Unable to get IP Address";
        struct ifaddrs *interfaces = NULL;
        struct ifaddrs *temp_addr = NULL;
        int success = 0;
        // retrieve the current interfaces - returns 0 on success
        success = getifaddrs(&interfaces);
        if (success == 0) {
            // Loop through linked list of interfaces
            temp_addr = interfaces;
            while(temp_addr != NULL) {
                if(temp_addr->ifa_addr->sa_family == AF_INET) {
                    // Check if interface is the default interface
                    if(temp_addr->ifa_name == interface){
                        ipAddress=inet_ntoa(((struct sockaddr_in*)temp_addr->ifa_addr)->sin_addr);
                    }
                }
                temp_addr = temp_addr->ifa_next;
            }
        }
        // Free memory
        freeifaddrs(interfaces);
        return ipAddress;
    }

    void getPublicIPAddress() {
        httplib::Client cli("https://ifconfig.me");
        auto res = cli.Get("/ip");
        if ( res.error() == httplib::Error::SSLServerVerification ) {
            httplib::Client cli("http://ifconfig.me");
            res = cli.Get("/ip");
        }
        public_ip = res->body;
    }

    // -- SYSTEM INFO -- //
    void retrieve_hostname() {
        std::ifstream hostinfo ("/etc/hostname", std::ifstream::in);
        hostinfo >> hostname;
    }

    void retrieve_os_version() {
        std::ifstream osinfo ("/etc/os-release", std::ifstream::in);
        std::string line;

        while(std::getline(osinfo,line)) {
            if (line.find("PRETTY_NAME") != std::string::npos) {
                // get text in between "
                unsigned first_del = line.find('"');
                unsigned last_del = line.find_last_of('"');
                os_version = line.substr(first_del +1, last_del - first_del -1);
            }
        }
    }

    void retrieve_kernel_version() {
        std::ifstream kernelinfo ("/proc/version", std::ifstream::in);
        std::string version;

        kernelinfo >> kernel_version;
        kernelinfo >> version; // skipping second word
        kernelinfo >> version;

        kernel_version = kernel_version + " " + version;
    }

    void retrieve_uptime() {
        std::chrono::milliseconds uptime_millis(0u);
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

    // -- NETWORK -- //
    void retrieve_ifc_stats() {
        for (auto & ifc : interfaces_) {
            int j = 0;

            std::ifstream netinfo ("/proc/net/dev", std::ifstream::in);
            std::string line;
            std::string null;

            while(netinfo >> line) {
                if (line == ifc.name+":") {
                    netinfo >> ifc.total_transfer[1][j++]; //first bytes inc
                    netinfo >> ifc.total_packets[j-1]; // total packets inc
                    netinfo >> null >> ifc.total_packets[j]; // drop packets inc
                    netinfo >> null >> null >> null >> null >> ifc.total_transfer[1][j++]; // total bytes out
                    netinfo >> ifc.total_packets[j]; // total packets out
                    netinfo >> null >> ifc.total_packets[j+1]; // drop packets out

                    ifc.total_transfer[1][j++] = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    break;
                }
            }
        }
    }

    // -- STORAGE -- //
    void retrieve_fs_stats() {
        for (auto & fs : filesystems_) {
            fs.space_info = std::filesystem::space(fs.path);
        }
    }

    // -- RAM -- //
    void retrieve_ram() {

        std::ifstream raminfo ("/proc/meminfo", std::ifstream::in);
        std::string line;

        while(raminfo >> line) {
            if (line == "MemTotal:") {
                raminfo >> ram_total;
            } else if (line == "MemAvailable:") {
                raminfo >> ram_available;
            } else if (line == "SwapTotal:") {
                raminfo >> ram_swaptotal;
            } else if (line == "SwapFree:") {
                raminfo >> ram_swapfree;
                // From /proc/meminfo order once we reach available we may stop reading
                break;
            }
        }
    }

    // -- CPU -- //
    void retrieve_cpu_cores() {
        cpu_cores = std::thread::hardware_concurrency();
    }

    void retrieve_cpu_loads() {
        std::ifstream loadinfo ("/proc/loadavg", std::ifstream::in);
        for (auto i = 0; i < 3; i++) {
            loadinfo >> cpu_loads[i];
        }
    }

    void retrieve_cpu_usage() {
        cpu_usage = cpu_loads[0] * 100 / cpu_cores;
    }

    void retrieve_cpu_procs() {
        std::string path = "/proc";
        cpu_procs = 0;
        for (const auto & entry : std::filesystem::directory_iterator(path)) {
          if (entry.is_directory() && isdigit(entry.path().u8string().back()))  {
            cpu_procs++;
          }
        }
    }

    // -- I/O -- //
    void retrieve_dv_stats() {
        for(auto & dv : drives_) {
            int j = 0;

            std::ifstream dvinfo ("/sys/block/"+dv.name+"/stat", std::ifstream::in);
            std::string null;

            dvinfo >> null >> null >> dv.total_io[1][j++]; // sectors read
            dvinfo >> null >> null >> null >> dv.total_io[1][j++]; // sectors written
            dvinfo >> null >> null >> dv.total_io[1][j++]; // io ticks -> time spent in io

            dv.total_io[1][j++] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        }
    }

};
