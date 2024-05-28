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

#include "monitor/monitor.hpp"

namespace fs = std::filesystem;

#if OPEN_SSL
  #define CPPHTTPLIB_OPENSSL_SUPPORT
#endif



namespace thinger::monitor {

    class Client {

    public:

        Client(thinger::iotmp::client& client, Config& config) :
            resources_{
              {"backup", client["backup"]},
              {"restore", client["restore"]},
            },
            monitor_(client, config, server_),
            config_(config)
        {
          //monitor_.initialize(client, config, server_);

            /*if ( ! config_.get_backup().empty() ) {
              resources_.insert( {"backup", client["backup"]});
              resources_.insert( {"restore", client["restore"]});
            }*/

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
            monitor_.reload();
          }

          // TODO: use jthread stop_token
          server_.stop();
          start_local_server();

        } else {
          every5s = every1m = every5m = every1d = 0;
        }

    }

protected:

    // TODO: move this functions to ns and make them configurable or self discoverable
    // -- SYSTEM INFO -- //
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

private:

    std::unordered_map<std::string, iotmp::iotmp_resource&> resources_;

    // local server for resources
    httplib::Server server_;
    std::jthread svr_jthread;

    thinger::monitor::monitor monitor_;

    Config& config_;

    struct future_task {
        std::string task;
        std::future<void> future;
    };
    future_task f1; // f1 used for blocking backup/restore/update/update_distro

    };

}
