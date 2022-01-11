// Create backup

// backups mongodb

// backup influxdb

#include <filesystem>
#include <fstream>

#include "../../utils/date.h"
#include "../../utils/aws.h"


class ThingerBackup {

public:

    ThingerBackup(ThingerMonitorConfig& config, std::string hostname) : config_(config), hostname_(hostname) {

        system_app = config_.get_backups_system();
        storage = config_.get_backups_storage();
        bucket = config_.get_backups_bucket();
        region = config_.get_backups_region();
        access_key = config_.get_backups_access_key();
        secret_key = config_.get_backups_secret_key();

        file_to_upload = hostname_+"_"+backup_date+".tar.gz";

        create_backup_folder();
    }

    void create_backup() {

        if ( system_app == "platform" ) {
            backup_thinger();
            backup_mongodb();
            backup_influxdb();
            compress_backup();
        }
        // else if

    }

    int upload_backup()  {

        if ( storage == "S3" )
            return AWS::upload_to_s3(backup_folder+"/"+file_to_upload, bucket, region, access_key, secret_key);
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
    const std::string backup_date = Date::now_iso8601();

    ThingerMonitorConfig& config_;
    std::string hostname_;

    std::string file_to_upload;

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

    void compress_backup() { // TODO: use some tar library?
        std::ofstream output(backup_folder+"/"+file_to_upload); // create empty file before excluding
        std::string command = "tar --exclude="+file_to_upload+" -zcf "+backup_folder+"/"+file_to_upload+" -C "+backup_folder+" .";
std::cout << command << std::endl;
        system(command.c_str());
    }

    // -- PLATFORM -- //
    void backup_thinger() {
        std::filesystem::create_directories(backup_folder+"/thinger-"+backup_date);
        std::filesystem::copy(config_.get_backups_data_path()+"/thinger/users", backup_folder+"/thinger-"+backup_date+"/users", std::filesystem::copy_options::recursive);
        //std::filesystem::copy(config_.get_backups_data_path()+"/thinger/users", backup_folder+"/thinger-"+backup_date);
    }

    void backup_mongodb() {
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
        system(("docker exec mongodb mongodump -u \"thinger\" -p \""+mongo_password+"\" >> /dev/null").c_str());
        system(("docker cp mongodb:dump "+backup_folder+"/mongodbdump-"+backup_date+" >> /dev/null").c_str());
    }

    void backup_influxdb() {
        // TODO: execute influxd backup and copy through docker REST API
        system("docker exec influxdb influxd backup --portable /dump >> /dev/null");
        system(("docker cp mongodb:dump "+backup_folder+"/influxdbdump-"+backup_date+" >> /dev/null").c_str());
    }

    void clean_thinger() {
        std::filesystem::remove_all(backup_folder);
        // TODO: clean inside docker containers through docker REST API
        system("docker exec mongodb rm -rf /dump");
        system("docker exec influxdb rm -rf /dump");
    }

};
