
#include "../restore.h"

#include <filesystem>
#include <fstream>

#include "../../utils/date.h"
#include "../../utils/aws.h"
#include "../../utils/tar.h"
#include "../../utils/tar.h"

class PlatformRestore : public ThingerMonitorRestore {

public:

    PlatformRestore(ThingerMonitorConfig& config, const std::string hostname, const std::string tag)
      : ThingerMonitorRestore(config,hostname,tag) {

        storage = config_.get_backups_storage();
        bucket = config_.get_storage_bucket(storage);
        region = config_.get_storage_region(storage);
        access_key = config_.get_storage_access_key(storage);
        secret_key = config_.get_storage_secret_key(storage);

        file_to_download = name_+"_"+tag_+".tar.gz";

        create_backup_folder();
    }

    void restore() {

        decompress_backup();
        restore_thinger();
        restore_mongodb();
        restore_influxdb();
        restore_plugins();
        restart_platform();

    }

    int download()  {

        if ( storage == "S3" )
            return AWS::download_from_s3(backup_folder+"/"+file_to_download, bucket, region, access_key, secret_key);
        // else if
        return -1;
    }

    void clean() {
        clean_thinger();
    }

protected:
    const std::string backup_folder = "/tmp/backup";

    std::string file_to_download;

    std::string storage;
    std::string bucket;
    std::string region;
    std::string access_key;
    std::string secret_key;


private:
    void create_backup_folder() {
//        std::filesystem::remove_all(backup_folder);
        std::filesystem::create_directories(backup_folder+"/"+tag_);
    }

    void decompress_backup() {
        Tar::extract(backup_folder+"/"+file_to_download);
    }

    // -- PLATFORM -- //
    void restore_thinger() {
        Docker::Container::stop("thinger");
        if (std::filesystem::exists(backup_folder+"/"+tag_+"/thinger-"+tag_+".tar")) {
            std::filesystem::remove_all(config_.get_backups_data_path()+"/thinger/users");
            Tar::extract(backup_folder+"/"+tag_+"/thinger-"+tag_+".tar");
        }
    }

    void restore_mongodb() {
        // get mongodb root password
        std::ifstream compose (config_.get_backups_compose_path()+"/docker-compose.yml", std::ifstream::in);
        std::string line;

        std::string mongo_password;

        while(std::getline(compose,line,'\n')) {
            if (line.find("- MONGO_INITDB_ROOT_PASSWORD") != std::string::npos) {
                unsigned first_del = line.find('=');
                unsigned last_del = line.find('\n');
                mongo_password = line.substr(first_del+1, last_del - first_del-1);
            }
        }

        Docker::Container::copy_to_container("mongodb", backup_folder+"/"+tag_+"/mongodbdump-"+tag_+".tar", "/");
        Docker::Container::exec("mongodb", "mongorestore /dump -u thinger -p "+mongo_password);
    }

    void restore_influxdb() {
        if (std::filesystem::exists(config_.get_backups_data_path()+"/influxdb2")) {
            Docker::Container::copy_to_container("influxdb2", backup_folder+"/"+tag_+"/influxdb2dump-"+tag_+".tar", "/");
            Docker::Container::exec("influxdb2", "mkdir -p /var/lib/influxdb2/tmp/");
            Docker::Container::exec("influxdb2", "chown influxdb:influxdb /var/lib/influxdb2/tmp/");
            Docker::Container::exec("influxdb2", "influx restore /dump --full");
        } else {
            Docker::Container::copy_to_container("influxdb", backup_folder+"/"+tag_+"/influxdbdump-"+tag_+".tar", "/");
            Docker::Container::exec("influxdb", "influxd restore --portable /dump");
        }
    }

    void restore_plugins() {
        // Executed after restore_thinger
        if (!std::filesystem::exists(config_.get_backups_data_path()+"/thinger/users/")) return;

        for (const auto & p1 : fs::directory_iterator(config_.get_backups_data_path()+"/thinger/users/")) { // users
            if (! std::filesystem::exists(p1.path().string()+"/plugins/")) continue;

            std::string user = p1.path().filename().string();

            // restore networks
            std::string network_id = Docker::Network::create_from_inspect(backup_folder+"/"+tag_+"/plugins/"+user+"-network.json");

            // restore plugins
            for (const auto & p2 : fs::directory_iterator(p1.path().string()+"/plugins/")) { // plugins
                std::string container_name = user+"-"+p2.path().filename().string();
                Docker::Container::create_from_inspect(backup_folder+"/"+tag_+"/plugins/"+container_name+".json", network_id);
                Docker::Container::start(container_name);
            }
        }
    }

    void clean_thinger() {
        std::filesystem::remove_all(backup_folder+"/"+file_to_download);
        std::filesystem::remove_all(backup_folder+"/"+tag_);
        Docker::Container::exec("mongodb", "rm -rf /dump");
        if (std::filesystem::exists(config_.get_backups_data_path()+"/influxdb2")) {
            Docker::Container::exec("influxdb2", "rm -rf /dump");
        } else {
            Docker::Container::exec("influxdb", "rm -rf /dump");
        }
    }

    void restart_platform() {
        Docker::Container::restart("mongodb");
        Docker::Container::restart("influxdb");
        Docker::Container::restart("thinger");
    }

};
