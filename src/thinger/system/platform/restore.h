// Create backup

// backups mongodb

// backup influxdb

#include <filesystem>
#include <fstream>

#include "../../utils/date.h"
#include "../../utils/aws.h"


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
        std::filesystem::remove_all(backup_folder);
        std::filesystem::create_directories(backup_folder);
    }

    void decompress_backup() { // TODO: use some tar library?
        std::string command = "tar xfz "+backup_folder+"/"+file_to_download+" -C "+backup_folder+" --same-owner";
        system(command.c_str());
    }

    // -- PLATFORM -- //
    void restore_thinger() {
        // TODO: user docker REST API
        system("docker stop thinger");
        std::filesystem::remove_all(config_.get_backups_data_path()+"/thinger/users");
        std::filesystem::create_directories(config_.get_backups_data_path()+"/thinger/users");
        std::filesystem::copy(backup_folder+"/thinger-"+tag_+"/users",config_.get_backups_data_path()+"/thinger/users", std::filesystem::copy_options::recursive);
        //std::filesystem::copy(config_.get_backups_data_path()+"/thinger/users", backup_folder+"/thinger-"+backup_date);
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

        // TODO: execute mongodump and copy through docker REST API
        system(("docker cp "+backup_folder+"/mongodbdump-"+tag_+" mongodb:/dump >> /dev/null").c_str());
        system(("docker exec mongodb mongorestore /dump -u \"thinger\" -p \""+mongo_password+"\" >> /dev/null").c_str());
    }

    void restore_influxdb() {
        // TODO: execute influxd backup and copy through docker REST API
        system(("docker cp "+backup_folder+"/influxdbdump-"+tag_+" influxdb:/dump >> /dev/null").c_str());
        system("docker exec influxdb influxd restore --portable /dump >> /dev/null");
    }

    void clean_thinger() {
        std::filesystem::remove_all(backup_folder);
        // TODO: clean inside docker containers through docker REST API
        system("docker exec mongodb rm -rf /dump");
        system("docker exec influxdb rm -rf /dump");
    }

    void restart_platform() {
        system(("docker-compose -f "+config_.get_backups_compose_path()+"/docker-compose.yml restart").c_str());
    }

};
