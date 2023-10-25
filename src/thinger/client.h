#include "config.h"
#include "monitor.h"

#include <httplib.h>

#include <filesystem>
#include <linux/kernel.h>
#include <unistd.h>
#include <thread>
#include <future>

#include "utils/thinger.h"
#include "utils/date.h"

#include "system/platform/backup.h"
#include "system/platform/restore.h"

namespace fs = std::filesystem;

#if OPEN_SSL
  #define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#define STR_(x) #x
#define STR(x) STR_(x)

#define VERSION STR(BUILD_VERSION) // BUILD_VERSION must always be defined

constexpr int SECTOR_SIZE = 512;

// Conversion constants. //
[[maybe_unused]] const long minute = 60;
[[maybe_unused]] const long hour = minute * 60;
[[maybe_unused]] const long day = hour * 24;

[[maybe_unused]] const std::uintmax_t btokb = 1024;
[[maybe_unused]] const std::uintmax_t kbtogb = btokb * 1024;
[[maybe_unused]] const std::uintmax_t btomb = kbtogb;
[[maybe_unused]] const std::uintmax_t btogb = kbtogb * 1024;

namespace thinger::monitor {

    class Client {

    public:

        Client(thinger::iotmp::client& client, Config& config) :
            resources_{
              {"monitor", client["monitor"](server_)},
              {"cmd", client["cmd"]},
              {"reboot", client["reboot"]},
              {"update", client["update"]},
              {"update_distro", client["update_distro"]},
              {"backup", client["backup"]},
              {"restore", client["restore"]},
            },
            config_(config)
        {

            /*if ( ! config_.get_backup().empty() ) {
              resources_.insert( {"backup", client["backup"]});
              resources_.insert( {"restore", client["restore"]});
            }*/

            // Executed only once
            system::retrieve_hostname(hostname);
            system::retrieve_os_version(os_version);
            system::retrieve_kernel_version(kernel_version);
            cpu::retrieve_cpu_cores(cpu_cores);

            resources_.at("cmd") = [this, &client](iotmp::input& in, iotmp::output& out) {
                std::string output = cmd(in["input"]);
                out["output"] = output;

                std::string endpoint = in["endpoint"];
                in["endpoint"] = "cmd_finished";

                if (!endpoint.empty()) {
                    pson payload;
                    payload["device"] = config_.get_id();
                    payload["hostname"] = hostname;
                    payload["payload"] = output;
                    client.call_endpoint(endpoint.c_str(), payload);
                }
            };

            if (geteuid() == 0) { // is_root
                resources_.at("reboot") = [](iotmp::input& in) { // needs declaration of input for dashboard button
                    if (in)
                        reboot();
                };

                resources_.at("update") = [this](iotmp::input& in, iotmp::output& out) { // needs declaration of input for dashboard button
                    if (in) {
                        if ( f1.future.valid() && f1.future.wait_for(std::chrono::seconds(0))  != std::future_status::ready ) {
                            if ( f1.task == "update" )
                                out["status"] = "Already executing";
                            else
                                out["status"] = ("Executing: "+f1.task).c_str();
                            return;
                        }

                        std::packaged_task<void()> task([]{
                            update();
                        });
                        f1.task = "update";
                        f1.future = task.get_future();
                        std::thread thread(std::move(task));
                        thread.detach();
                    }
                };

                resources_.at("update_distro") = [this](iotmp::input& in, iotmp::output& out) { // needs declaration of input for dashboard button
                    if (in) {
                        if ( f1.future.valid() && f1.future.wait_for(std::chrono::seconds(0))  != std::future_status::ready ) {
                            if ( f1.task == "update_distro" )
                                out["status"] = "Already executing";
                            else
                                out["status"] = ("Executing: "+f1.task).c_str();
                            return;
                        }

                        std::packaged_task<void()> task([]{
                            update_distro();
                        });
                        f1.task = "update_distro";
                        f1.future = task.get_future();
                        std::thread thread(std::move(task));
                        thread.detach();
                    }
                };
            }

            //if ( ! config_.get_backup().empty() ) {
              resources_.at("backup") = [this, &client](iotmp::input &in, iotmp::output &out) {

                if (!config_.get_backup().empty()) {

                  std::string tag = in["tag"];
                  std::string endpoint = in["endpoint"];

                  auto today = Date();
                  in["tag"] = today.to_iso8601();
                  in["endpoint"] = "backup_finished";

                  if (f1.future.valid() && f1.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    if (f1.task == "backup")
                      out["status"] = "Already executing";
                    else
                      out["status"] = ("Executing: " + f1.task).c_str();
                    return;
                  }

                  // future from a packaged_task
                  std::packaged_task<void(std::string, std::string)> task(
                      [this, &client](const std::string &task_tag, const std::string &task_endpoint) {

                        std::unique_ptr<ThingerMonitorBackup> backup{}; // as nullptr
                        // Add new possible options for backup systems
                        if (config_.get_backup() == "platform") {
                          backup = std::make_unique<PlatformBackup>(config_, hostname, task_tag);
                        }

                        json data;
                        data["device"] = config_.get_id();
                        data["hostname"] = hostname;
                        data["backup"] = {};
                        data["backup"]["operations"] = {};

                        LOG_INFO("[_BACKUP] Creating backup");
                        data["backup"]["operations"]["backup"] = backup->backup();
                        LOG_INFO("[_BACKUP] Uploading backup");
                        data["backup"]["operations"]["upload"] = backup->upload();

                        // Clean if upload succeded
                        if ( data["backup"]["operations"]["upload"]["status"].get<bool>() ) {
                          LOG_INFO("[_BACKUP] Cleaning backup temporary files");
                          data["backup"]["operations"]["clean"] = backup->clean();
                        } else {
                          LOG_WARNING("[_BACKUP] Left local copy of backup due to failed upload");
                        }

                        data["backup"]["status"] = true;
                        for (auto &element: data["backup"]["operations"]) {
                          if (!element["status"].get<bool>()) {
                            data["backup"]["status"] = false;
                            break;
                          }
                        }

                        //LOG_LEVEL(1, "[_BACKUP] Backup status: {0}", data.dump());
                        LOG_LEVEL(1, "[_BACKUP] Backup status: %s", data.dump());
                        if (!task_endpoint.empty()) {
                          protoson::pson payload;
                          protoson::json_decoder::parse(data, payload);
                          client.call_endpoint(task_endpoint.c_str(), payload);
                        }

                      });

                  if (!tag.empty()) {
                    out["status"] = "Launched";
                    f1.task = "backup";
                    f1.future = task.get_future();  // get a future
                    std::thread thread(std::move(task), tag, endpoint);
                    thread.detach();
                  }

                  out["status"] = "Ready to be launched";

                } else {
                  out["status"] = "ERROR";
                  out["error"] = "Can't launch backup. Set backups property.";
                }
              };

              resources_.at("restore") = [this, &client](iotmp::input &in, iotmp::output &out) {

                if (!config_.get_backup().empty()) {

                  std::string tag = in["tag"];
                  std::string endpoint = in["endpoint"];

                  auto today = Date();
                  in["tag"] = today.to_iso8601();
                  in["endpoint"] = "restore_finished";

                  if (f1.future.valid() && f1.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    if (f1.task == "restore")
                      out["status"] = "Already executing";
                    else
                      out["status"] = ("Executing: " + f1.task).c_str();
                    return;
                  }

                  // TODO: Catch exception in thread
                  std::packaged_task<void(std::string, std::string)> task(
                      [this, &client](const std::string &task_tag, const std::string &task_endpoint) {

                        std::unique_ptr<ThingerMonitorRestore> restore{};
                        // Add new possible options for backup systems
                        if (config_.get_backup() == "platform") {
                          restore = std::make_unique<PlatformRestore>(config_, hostname, task_tag);
                        }

                        json data;
                        data["device"] = config_.get_id();
                        data["hostname"] = hostname;
                        data["restore"] = {};
                        data["restore"]["operations"] = {};

                        LOG_INFO("[___RSTR] Downloading backup");
                        data["restore"]["operations"]["download"] = restore->download();
                        LOG_INFO("[___RSTR] Restoring backup");
                        data["restore"]["operations"]["restore"] = restore->restore();
                        LOG_INFO("[___RSTR] Cleaning backup temporary files");
                        data["restore"]["operations"]["clean"] = restore->clean();
                        //LOG_LEVEL(1, "[___RSTR] Restore status: {0}", data.dump());
                        LOG_LEVEL(1, "[___RSTR] Restore status: %s", data.dump());
                        if (!task_endpoint.empty()) {
                          protoson::pson payload;
                          protoson::json_decoder::parse(data, payload);
                          client.call_endpoint(task_endpoint.c_str(), payload);
                        }

                      });

                  if (!tag.empty()) {
                    out["status"] = "Launched";
                    f1.task = "restore";
                    f1.future = task.get_future();
                    std::thread thread(std::move(task), tag, endpoint);
                    thread.detach();
                  }

                  out["status"] = "Ready to be launched";

                } else {
                  out["status"] = "ERROR";
                  out["error"] = "Can't launch restore. Set backups property.";
                }
              };
            //}

            resources_.at("monitor") = [this](iotmp::output& out) {

                unsigned long current_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                // Set values at defined intervals
                if (current_seconds >= (every5s + 5)) {
                    cpu::retrieve_cpu_loads(cpu_loads); // load and usage is updated every 5s by sysinfo
                    cpu::retrieve_cpu_usage(cpu_usage, cpu_loads, cpu_cores);
                    every5s = current_seconds;
                }

                if (current_seconds >= (every1m + 60)) {
                    system::retrieve_uptime(uptime);
                    cpu::retrieve_cpu_procs(cpu_procs);
                    storage::retrieve_fs_stats(filesystems_);
                    every1m = current_seconds;
                }

                if (current_seconds >= (every5m + 60*5)) {
                    retrieve_restart_status();
                    retrieve_updates();
                    if (config_.get_backup() == "platform")
                      platform::getConsoleVersion(console_version);
                    every5m = current_seconds;
                }

                if (current_seconds >= (every1d + 60*60*24)) {
                    public_ip = network::getPublicIPAddress();
                    // FIXME: config get_backup is not set on first execution since
                    every1d = current_seconds;
                }

                // Storage
                for (auto const & fs : filesystems_) {
                    std::string name = fs.path;
                    if ( config_.get_defaults() && &fs == &filesystems_.front()) {
                        name = "default";
                    }
                    out[("st_"+name+"_capacity").c_str()] = std::trunc( (float)fs.space_info.capacity / (float)btogb * 100 ) / 100;
                    out[("st_"+name+"_used").c_str()] = std::trunc( (float)(fs.space_info.capacity - fs.space_info.free) / (float)btogb * 100 ) / 100;
                    out[("st_"+name+"_free").c_str()] = std::trunc( (float)fs.space_info.free / (float)btogb * 100 ) / 100;
                    out[("st_"+name+"_usage").c_str()] = std::trunc( ((float)(fs.space_info.capacity - fs.space_info.free)*100) / (float)fs.space_info.capacity * 100 ) / 100; // usage based on time of io spend doing io operations
                }

                // IO
                retrieve_dv_stats(drives_);
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

                    out[("dv_"+name+"_speed_read").c_str()] = std::trunc( speed_reading / btokb * 100 ) / 100;
                    out[("dv_"+name+"_speed_written").c_str()] = std::trunc( speed_writing / btokb * 100 ) / 100;
                    out[("dv_"+name+"_usage").c_str()] = usage < 1 ? std::trunc( usage * 100 * 100 ) / 100 : 100;

                    // flip matrix for speed and usage calculations
                    for (int z = 0; z < 4; z++) {
                        dv.total_io[0][z] = dv.total_io[1][z];
                    }
                }

                // Network
                retrieve_ifc_stats(interfaces_);
                for (auto & ifc : interfaces_) {
                    std::string name = ifc.name;
                    if ( config_.get_defaults() && &ifc == &interfaces_.front()) {
                        name = "default";
                    }

                    out[("nw_"+name+"_internal_ip").c_str()] = ifc.internal_ip;

                    out[("nw_"+name+"_transfer_incoming").c_str()]    = std::trunc( (float)ifc.total_transfer[0][1] / (float)btogb * 100 ) / 100;
                    out[("nw_"+name+"_transfer_outgoing").c_str()]    = std::trunc( (float)ifc.total_transfer[1][1] / (float)btogb * 100 ) / 100;
                    out[("nw_"+name+"_transfer_total").c_str()]       = std::trunc( ((float)ifc.total_transfer[0][1] + (float)ifc.total_transfer[1][1]) / (float)btogb * 100 ) / 100;
                    out[("nw_"+name+"_packetloss_incoming").c_str()]  = ifc.total_packets[1];
                    out[("nw_"+name+"_packetloss_outgoing").c_str()]  = ifc.total_packets[3];

                    // speeds in B/s
                    float speed_incoming = ((float)(ifc.total_transfer[0][1] - ifc.total_transfer[0][0]) /
                        (float)(ifc.total_transfer[2][1] - ifc.total_transfer[2][0]))*1000;
                    float speed_outgoing = ((float)(ifc.total_transfer[1][1] - ifc.total_transfer[1][0]) /
                        (float)(ifc.total_transfer[2][1] - ifc.total_transfer[2][0]))*1000;

                    out[("nw_"+name+"_speed_incoming").c_str()] = std::trunc( speed_incoming * 8 / btokb * 100 ) / 100;
                    out[("nw_"+name+"_speed_outgoing").c_str()] = std::trunc( speed_outgoing * 8 / btokb * 100 ) / 100;
                    out[("nw_"+name+"_speed_total").c_str()]    = std::trunc( ((speed_incoming + speed_outgoing) * 8) / btokb * 100 ) / 100;

                    // flip matrix for speed and usage calculations
                    for (int i = 0; i < 3; i++) {
                        ifc.total_transfer[i][0] = ifc.total_transfer[i][1];
                    }
                }
                out["nw_public_ip"] = public_ip;

                if (config_.get_backup() == "platform") {
                    out["console_version"] = console_version;
                }

                // RAM
                memory::retrieve_ram(ram_total, ram_available, ram_swaptotal, ram_swapfree);
                out["ram_total"] = std::trunc( (float)ram_total / kbtogb * 100 ) / 100;
                out["ram_available"] = std::trunc( (float)ram_available / kbtogb * 100) / 100;
                out["ram_used"] = std::trunc( (float)(ram_total - ram_available) / kbtogb * 100 ) / 100;
                out["ram_usage"] = std::trunc( (float)((ram_total - ram_available) * 100) / (float)ram_total * 100 ) / 100;
                out["ram_swaptotal"] = std::trunc( (float)ram_swaptotal / (float)kbtogb * 100 ) / 100;
                out["ram_swapfree"] = std::trunc( (float)ram_swapfree / kbtogb * 100 ) / 100;
                out["ram_swapused"] = std::trunc( (float)(ram_swaptotal - ram_swapfree) / kbtogb * 100) / 100;
                out["ram_swapusage"] = (ram_swaptotal == 0) ? 0 : std::trunc( (float)((ram_swaptotal - ram_swapfree) *100) / (double)ram_swaptotal * 100 ) / 100;

                // CPU
                out["cpu_cores"] = cpu_cores;
                out["cpu_load_1m"] = cpu_loads[0];
                out["cpu_load_5m"] = cpu_loads[1];
                out["cpu_load_15m"] = cpu_loads[2];
                out["cpu_usage"] = std::trunc(cpu_usage*100)/100;
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

            start_local_server();
    }

    void start_local_server() {
        svr_jthread = std::jthread( [this](const std::stop_token& stoken) {
          if (stoken.stop_requested()) { // FIXME: the thread hangs onto the server_.listen and can never get here
                server_.stop();
                return;
          }
          THINGER_LOG("Creating local server in %s and port %hd", config_.get_svr_host(), config_.get_svr_port());
          while ( server_.is_running() )
            std::this_thread::sleep_for(std::chrono::milliseconds{10});

          server_.listen(config_.get_svr_host(), config_.get_svr_port());
        }
      );
    }

    virtual ~Client() {
        THINGER_LOG("stopping monitoring client");

        // stop httplib server on shutdown
        server_.stop();//TODO: svr_jthread.request_stop();
    }

    // Recreates and fills up structures
    void reload_configuration(std::string_view const& property) {

        if ( "resources" == property ) {
          interfaces_.clear();
          filesystems_.clear();
          drives_.clear();

          for (const auto& fs_path : config_.get_filesystems()) {
            storage::filesystem fs;
            fs.path = fs_path;
            filesystems_.push_back(fs);
          }
          retrieve_fs_stats(filesystems_);

          for (const auto& dv_name : config_.get_drives()) {
            io::drive dv;
            dv.name = dv_name;
            drives_.push_back(dv);
          }
          retrieve_dv_stats(drives_);

          for (const auto& ifc_name : config_.get_interfaces()) {
            network::interface ifc;
            ifc.name = ifc_name;
            ifc.internal_ip = network::getIPAddress(ifc.name);
            interfaces_.push_back(ifc);
          }
          retrieve_ifc_stats(interfaces_);

          // stop monitor server
          server_.stop();//TODO: svr_jthread.request_stop();
          start_local_server();

        } else {
          every5s = every1m = every5m = every1d = 0;
        }

    }

protected:

    // TODO: move this functions to ns and make them configurable or self discoverable
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

    static std::string cmd(const char *in) {
        std::array<char, 128> buffer{};
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(in, "r"), &pclose);
        if (!pipe) {
            return "Failed to run command\n";
        }

        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += std::string(buffer.data());
        }
        return result;
    }

    static void reboot() {
        ::system("sudo reboot");
    }

    static void update() {
        // System upgrade. By default it does not overwrite config files if a package has a newer version
        ::system("sudo apt -y update && sudo DEBIAN_FRONTEND=noninteractive DEBIAN_PRIORITY=critical UCF_FORCE_CONFFOLD=1 apt -qq -y -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold upgrade");
    }

    static void update_distro() {
        // Full unattended distro upgrade
        ::system("sudo apt -y update && sudo do-release-upgrade -f DistUpgradeViewNonInteractive");
    }

private:

    std::unordered_map<std::string, iotmp::iotmp_resource&> resources_;

    Config& config_;

    struct future_task {
        std::string task;
        std::future<void> future;
    };
    future_task f1; // f1 used for blocking backup/restore/update/update_distro

    // network
    std::vector<network::interface> interfaces_;
    std::string public_ip;

    // storage
    std::vector<storage::filesystem> filesystems_;

    // io
    std::vector<io::drive> drives_;

    // system info
    std::string hostname;
    std::string os_version;
    std::string kernel_version;
    std::string uptime;
    unsigned int normal_updates = 0;
    unsigned int security_updates = 0;
    bool system_restart = false;

    // CPU
    std::array<float, 3> cpu_loads; // 1, 5 and 15 mins loads
    float cpu_usage;
    unsigned int cpu_cores;
    unsigned int cpu_procs;

    // ram
    unsigned long ram_total;
    unsigned long ram_available;
    unsigned long ram_swaptotal;
    unsigned long ram_swapfree;

    // timing variables
    unsigned long every5s = 0;
    unsigned long every1m = 0;
    unsigned long every5m = 0;
    unsigned long every1d = 0;

    // thinger.io platform
    std::string console_version;

    // local server for resources
    httplib::Server server_;
    std::jthread svr_jthread;

    };

}
