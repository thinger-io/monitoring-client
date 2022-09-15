
#include "../backup.h"

#include <filesystem>
#include <fstream>

#include "../../utils/aws.h"
#include "../../utils/docker.h"
#include "../../utils/tar.h"

#include "./utils.h"

namespace fs = std::filesystem;

class PlatformBackup : public ThingerMonitorBackup {

public:

    PlatformBackup(thinger::monitor::Config& config, const std::string& hostname, const std::string& tag)
      : ThingerMonitorBackup(config,hostname, tag) {

        storage = this->config().get_storage();
        bucket = this->config().get_bucket(storage);
        region = this->config().get_region(storage);
        access_key = this->config().get_access_key(storage);
        secret_key = this->config().get_secret_key(storage);

        file_to_upload = this->name()+"_"+this->tag()+".tar.gz";

    }

    json backup() {

        json data;
        data["operation"] = {};
        data["operation"]["create_backup_folder"] = create_backup_folder();
        if (!data["operation"]["create_backup_folder"]["status"].get<bool>()) {
            data["status"] = false;
            return data;
        }

        data["operation"]["backup_thinger"]   = backup_thinger();
        data["operation"]["backup_mongodb"]   = backup_mongodb();
        data["operation"]["backup_influxdb"]  = backup_influxdb();
        data["operation"]["backup_plugins"]   = backup_plugins();
        data["operation"]["compress_backup"]  = compress_backup();

        // Set global status value
        data["status"] = true;
        for (auto& element : data["operation"]) {
            if (!element["status"].get<bool>()) {
                data["status"] = false;
                break;
            }
        }

        return data;
    }

    json upload()  {

        json data;

        if ( storage == "S3" )
            data["operation"]["upload_s3"] = {};
            if(AWS::multipart_upload_to_s3(backup_folder+"/"+file_to_upload, bucket, region, access_key, secret_key)) {
                data["operation"]["upload_s3"]["status"] = true;
            } else {
                data["operation"]["upload_s3"]["status"] = false;
                data["operation"]["upload_s3"]["error"].push_back("Failed uploading to S3");
            }
        // else if

        data["status"] = true;
        for (auto& element : data["operation"]) {
            if (!element["status"].get<bool>()) {
                data["status"] = false;
                break;
            }
        }

        return data;
    }

    json clean() {

        json data;

        clean_backup() ? data["status"] = true : data["status"] = false;

        return data;
    }

private:
    const std::string backup_folder = "/tmp/backup";

    std::string storage;
    std::string bucket;
    std::string region;
    std::string access_key;
    std::string secret_key;

    std::string file_to_upload;

    json create_backup_folder() const {
        json data;

        std::filesystem::remove_all(backup_folder+"/"+tag());
        if (!std::filesystem::create_directories(backup_folder+"/"+tag())) {
          data["status"]  = false;
          data["error"].push_back("Failed to create backup directory");
        } else
          data["status"]  = true;

        return data;
    }

    json backup_thinger() const {
        json data;

        // With tar creation instead of copying to folder we maintain ownership and permissions
        data["status"] = true;
        if (std::filesystem::exists(config().get_data_path()+"/thinger/users"))
            data["status"] = Tar::create(config().get_data_path()+"/thinger/users", backup_folder+"/"+tag()+"/thinger-"+tag()+".tar");

        return data;
    }

    json backup_mongodb() const {
        json data;
        // get mongodb root password
        std::ifstream compose (config().get_compose_path()+"/docker-compose.yml", std::ifstream::in);
        std::string line;

        std::string mongo_password;

        while(std::getline(compose,line,'\n')) {
            if (line.find("- MONGO_INITDB_ROOT_PASSWORD") != std::string::npos) {
                unsigned first_del = line.find('=');
                unsigned last_del = line.find('\n');
                mongo_password = line.substr(first_del+1, last_del - first_del-1);
            }
        }

        if (!Docker::Container::exec("mongodb", "mongodump -u thinger -p "+mongo_password)) {
            data["status"]  = false;
            data["error"].push_back("Failed executing mongodb backup");
            return data;
        }

        if (!Docker::Container::copy_from_container("mongodb", "/dump", backup_folder+"/"+tag()+"/mongodbdump-"+tag()+".tar")) {
            data["status"]  = false;
            data["error"].push_back("Failed copying mongodb backup from container");
            return data;
        }

        data["status"] = true;

        return data;
    }

    json backup_influxdb() const {
        json data;

        std::string influxdb_version = Platform::Utils::InfluxDB::get_version();

        if (influxdb_version.starts_with("v2.")) {
            // get influx token
            std::ifstream compose (config().get_compose_path()+"/docker-compose.yml", std::ifstream::in);
            std::string line;

            // TODO: docker_task Docker::Container::exec("influxdb2", "printenv --null DOCKER_INFLUXDB_INIT_ADMIN_TOKEN");

            std::string influx_token;

            while (std::getline(compose,line,'\n')) {
                if (line.find("- DOCKER_INFLUXDB_INIT_ADMIN_TOKEN") != std::string::npos) {
                    unsigned first_del = line.find('=');
                    unsigned last_del = line.find('\n');
                    influx_token = line.substr(first_del+1, last_del - first_del-1);
                }
            }
            if (!Docker::Container::exec("influxdb2", "influx backup /dump -t "+influx_token)) {
                data["status"]  = false;
                data["error"].push_back("Failed executing influxdb2 backup");
                return data;
            }
            if (!Docker::Container::copy_from_container("influxdb2", "/dump", backup_folder+"/"+tag()+"/influxdb2dump-"+tag()+".tar")) {
                data["status"]  = false;
                data["error"].push_back("Failed copying influxdb2 backup from container");
                return data;
            }
        } else if (influxdb_version.starts_with("1.")){
            if (!Docker::Container::exec("influxdb", "influxd backup --portable /dump")) {
                data["status"]  = false;
                data["error"].push_back("Failed executing influxdb backup");
                return data;
            }
            if (!Docker::Container::copy_from_container("influxdb", "/dump", backup_folder+"/"+tag()+"/influxdbdump-"+tag()+".tar")) {
                data["status"]  = false;
                data["error"].push_back("Failed copying influxdb backup from container");
                return data;
            }
        }

        data["status"] = true;

        return data;

    }

    json backup_plugins() const {

        json data;

        if (!std::filesystem::create_directories(backup_folder+"/"+tag()+"/plugins")) {
            data["status"]  = false;
            data["error"].push_back("Failed creating plugins directory in backup folder");
            return data;
        }

        if (! std::filesystem::exists(config().get_data_path()+"/thinger/users")) {
            data["status"] = true;
            data["msg"].push_back("Platform has no plugins");
            return data;
        }

        for (const auto & p1 : fs::directory_iterator(config().get_data_path()+"/thinger/users/")) { // users
            if (! std::filesystem::exists(p1.path().string()+"/plugins/")) continue;

            std::string user = p1.path().filename().string();

            // backups networks
            if ( Docker::Network::inspect(user, backup_folder+"/"+tag()+"/plugins") ) {
                data["msg"].push_back("Backed up "+user+" docker network");
            } else {
                data["error"].push_back("Failed backing up "+user+" docker network");
            }

            // backup plugins
            for (const auto & p2 : fs::directory_iterator(p1.path().string()+"/plugins/")) { // plugins
                std::string container_name = user+"-"+p2.path().filename().string();
                if (Docker::Container::inspect(container_name, backup_folder+"/"+tag()+"/plugins")) {
                    data["msg"].push_back("Backed up "+container_name+" docker container");
                } else {
                    data["error"].push_back("Failed backing up "+container_name+" docker container");
                }
            }
        }

        if (data.contains("error"))
            data["status"] = false;
        else
            data["status"] = true;

        return data;
    }

    json compress_backup() const {
        json data;

        data["status"] = Tar::create(backup_folder+"/"+tag(), backup_folder+"/"+file_to_upload);

        return data;
    }

    bool clean_backup() const {
        bool status = true;
        status = status && std::filesystem::remove_all(backup_folder+"/"+tag());
        status = status && std::filesystem::remove_all(backup_folder+"/"+file_to_upload);
        status = status && Docker::Container::exec("mongodb", "rm -rf /dump");

        std::string influxdb_version = Platform::Utils::InfluxDB::get_version();

        if (influxdb_version.starts_with("v2.")) {
            status = status && Docker::Container::exec("influxdb2", "rm -rf /dump");
        } else if (influxdb_version.starts_with("1.")){
            status = status && Docker::Container::exec("influxdb", "rm -rf /dump");
        }

        return status;
    }

};
