
#include "../backup.h"

#include <filesystem>
#include <fstream>

#include "../../utils/date.h"
#include "../../utils/aws.h"
#include "../../utils/docker.h"
#include "../../utils/tar.h"


class PlatformBackup : public ThingerMonitorBackup {

public:

    PlatformBackup(ThingerMonitorConfig& config, const std::string& hostname) : ThingerMonitorBackup(config,hostname) {
        storage = config_.get_backups_storage();
        bucket = config_.get_storage_bucket(storage);
        region = config_.get_storage_region(storage);
        access_key = config_.get_storage_access_key(storage);
        secret_key = config_.get_storage_secret_key(storage);

        file_to_upload = name_+"_"+backup_date+".tar.gz";

        create_backup_folder();
    }

    void create() {

        backup_thinger();
        backup_mongodb();
        backup_influxdb();
        get_plugins();
        compress_backup();

    }

    int upload()  {

        if ( storage == "S3" )
            return AWS::multipart_upload_to_s3(backup_folder+"/"+file_to_upload, bucket, region, access_key, secret_key);
            //return AWS::upload_to_s3(backup_folder+"/"+file_to_upload, bucket, region, access_key, secret_key);
        // else if
        return -1;
    }

    void clean() {
        clean_thinger();
    }

protected:
    const std::string backup_folder = "/tmp/backup";
    Date date = Date();
    const std::string backup_date = date.to_iso8601();

    std::string storage;
    std::string bucket;
    std::string region;
    std::string access_key;
    std::string secret_key;

    std::string file_to_upload;

private:
    void create_backup_folder() {
        std::filesystem::remove_all(backup_folder+"/"+backup_date);
        std::filesystem::create_directories(backup_folder+"/"+backup_date);
    }

    void compress_backup() {
        Tar::create(backup_folder+"/"+backup_date, backup_folder+"/"+file_to_upload);
    }

    void backup_thinger() {
        // With tar creation instead of copying to folder we maintain ownership and permissions
        if (std::filesystem::exists(config_.get_backups_data_path()+"/thinger/users"))
            Tar::create(config_.get_backups_data_path()+"/thinger/users", backup_folder+"/"+backup_date+"/thinger-"+backup_date+".tar");
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

        Docker::exec("mongodb", "mongodump -u thinger -p "+mongo_password);
        Docker::copy_from_container("mongodb", "/dump", backup_folder+"/"+backup_date+"/mongodbdump-"+backup_date+".tar");
    }

    void backup_influxdb() {
        Docker::exec("influxdb", "influxd backup --portable /dump");
        Docker::copy_from_container("influxdb", "/dump", backup_folder+"/"+backup_date+"/influxdbdump-"+backup_date+".tar");
    }

    void get_plugins() {

        std::filesystem::create_directories(backup_folder+"/"+backup_date+"/plugins");

        if (! std::filesystem::exists(config_.get_backups_data_path()+"/thinger/users")) return;

        for (const auto & p1 : fs::directory_iterator(config_.get_backups_data_path()+"/thinger/users/")) { // users
            if (! std::filesystem::exists(p1.path().string()+"/plugins/")) continue;
            for (const auto & p2 : fs::directory_iterator(p1.path().string()+"/plugins/")) { // plugins
                std::string container_name = p1.path().filename().string()+"-"+p2.path().filename().string();
                Docker::inspect(container_name, backup_folder+"/"+backup_date+"/plugins");
            }
        }
    }

    void clean_thinger() {
        std::filesystem::remove_all(backup_folder+"/"+backup_date);
        std::filesystem::remove_all(backup_folder+"/"+file_to_upload);
        Docker::exec("mongodb", "rm -rf /dump");
        Docker::exec("influxdb", "rm -rf /dump");
    }

};
