
#include <filesystem>
#include <fstream>

#include "../../utils/date.h"
#include "../../utils/aws.h"
#include "../../utils/tar.h"
#include "../../utils/tar.h"

class ThingerRestore {

public:

    ThingerRestore(ThingerMonitorConfig& config, std::string hostname, std::string tag) : config_(config), hostname_(hostname), tag_(tag) {

        system_app = config_.get_backups_system();
        storage = config_.get_backups_storage();
        bucket = config_.get_backups_bucket();
        region = config_.get_backups_region();
        access_key = config_.get_backups_access_key();
        secret_key = config_.get_backups_secret_key();

        file_to_download = hostname_+"_"+tag_+".tar.gz";

        create_backup_folder();
    }

    void restore_backup() {

        if ( system_app == "platform" ) {
            // TODO: Stop, all containers, remove data folders, restart, wait until it pings and launch restore
            decompress_backup();
            restore_thinger();
            restore_mongodb();
            restore_influxdb();
            restore_plugins();
            restart_platform();
        }
        // else if

    }

    int download_backup()  {

        if ( storage == "S3" )
            return AWS::download_from_s3(backup_folder+"/"+file_to_download, bucket, region, access_key, secret_key);
        // else if
        return -1;
    }

    void clean_backup() {
        if ( system_app == "platform" ) {
            clean_thinger();
        }
    }

protected:
    const std::string backup_folder = "/tmp/backup";

    ThingerMonitorConfig& config_;
    std::string hostname_;
    const std::string tag_;

    std::string file_to_download;

    std::string system_app;
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
        Docker::stop("thinger");
        std::filesystem::remove_all(config_.get_backups_data_path()+"/thinger/users");
        Tar::extract(backup_folder+"/"+tag_+"/thinger-"+tag_+".tar");
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

        Docker::copy_to_container("mongodb", backup_folder+"/"+tag_+"/mongodbdump-"+tag_+".tar", "/");
        Docker::exec("mongodb", "mongorestore /dump -u thinger -p "+mongo_password);
    }

    void restore_influxdb() {
        Docker::copy_to_container("influxdb", backup_folder+"/"+tag_+"/influxdbdump-"+tag_+".tar", "/");
        Docker::exec("influxdb", "influxd restore --portable /dump");
    }

    void restore_plugins() {
        // Executed after restore_thinger
        for (const auto & p1 : fs::directory_iterator(config_.get_backups_data_path()+"/thinger/users/")) { // users
            for (const auto & p2 : fs::directory_iterator(p1.path().string()+"/plugins/")) { // plugins
                std::string container_name = p1.path().filename().string()+"-"+p2.path().filename().string();
                Docker::create_from_inspect(backup_folder+"/"+tag_+"/"+container_name+".json");
                Docker::start(container_name);
            }
        }
    }

    void clean_thinger() {
        std::filesystem::remove_all(backup_folder+"/"+file_to_download);
        std::filesystem::remove_all(backup_folder+"/"+tag_);
        Docker::exec("mongodb", "rm -rf /dump");
        Docker::exec("influxdb", "rm -rf /dump");
    }

    void restart_platform() {
        Docker::restart("mongodb");
        Docker::restart("influxdb");
        Docker::restart("thinger");
    }

};
