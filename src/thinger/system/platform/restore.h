
#include "../restore.h"

#include <filesystem>
#include <fstream>

#include "../../utils/date.h"
#include "../../utils/aws.h"
#include "../../utils/tar.h"
#include "../../utils/tar.h"

#include "./utils.h"

class PlatformRestore : public ThingerMonitorRestore {

public:

    PlatformRestore(ThingerMonitorConfig& config, const std::string& hostname, const std::string& tag)
      : ThingerMonitorRestore(config,hostname,tag) {

        storage = this->config().get_backups_storage();
        bucket = this->config().get_storage_bucket(storage);
        region = this->config().get_storage_region(storage);
        access_key = this->config().get_storage_access_key(storage);
        secret_key = this->config().get_storage_secret_key(storage);

        file_to_download = this->name()+"_"+this->tag()+".tar.gz";

    }

    json restore() {

        json data;
        data["operation"] = {};
        data["operation"]["create_backup_folder"] = create_backup_folder();
        if (!data["operation"]["create_backup_folder"]["status"].get<bool>()) {
            data["status"] = false;
            return data;
        }

        data["operation"]["decompress_backup"] = decompress_backup();
        if (!data["operation"]["decompress_backup"]["status"].get<bool>()) {
            data["status"] = false;
            return data;
        }

        data["operation"]["retore_thinger"]   = restore_thinger();
        data["operation"]["restore_mongodb"]  = restore_mongodb();
        data["operation"]["restore_influxdb"] = restore_influxdb();
        data["operation"]["restore_plugins"]  = restore_plugins();
        data["operation"]["restart_platform"] = restart_platform();

        data["status"] = true;
        for (auto& element : data["operation"]) {
            if (!element["status"].get<bool>()) {
                data["status"] = false;
                break;
            }
        }

        return data;
    }

    json download()  {

        json data;

        if ( storage == "S3" )
            data["operation"]["download_s3"] = {};
            if (AWS::download_from_s3(backup_folder+"/"+file_to_download, bucket, region, access_key, secret_key)) {
                data["operation"]["download_s3"]["status"] = true;
            } else {
                data["operation"]["download_s3"]["status"] = false;
                data["operation"]["download_s3"]["error"].push_back("Failed downloading from S3");
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

        clean_restore() ? data["status"] = true : data["status"] = false;

       return data;
    }

private:
    const std::string backup_folder = "/tmp/backup";

    std::string file_to_download;

    std::string storage;
    std::string bucket;
    std::string region;
    std::string access_key;
    std::string secret_key;

    json create_backup_folder() const {
        json data;

        data["status"] = true;
        if ( !std::filesystem::exists(backup_folder+"/"+tag())
          && !std::filesystem::create_directories(backup_folder+"/"+tag()) ) {

            data["status"] = false;
            data["error"] = "Failed to create backup directory";
        }

        return data;
    }

    json decompress_backup() const {
        json data;

        data["status"] = Tar::extract(backup_folder+"/"+file_to_download);

        return data;
    }

    json restore_thinger() const {
        json data;

        if (!Docker::Container::stop("thinger")) {
            data["status"]  = false;
            data["error"].push_back("Failed stopping thinger container");
            return data;
        }

        if (std::filesystem::exists(backup_folder+"/"+tag()+"/thinger-"+tag()+".tar")) {
            if (!std::filesystem::remove_all(config().get_backups_data_path()+"/thinger/users")) {
                data["status"]  = false;
                data["error"].push_back("Failed removing thinger installed directories");
                return data;
            }

            if (!Tar::extract(backup_folder+"/"+tag()+"/thinger-"+tag()+".tar")) {
                data["status"]  = false;
                data["error"].push_back("Failed extracting thinger backup");
                return data;
            }
        }

        data["status"] = true;

        return data;
    }

    json restore_mongodb() const {
        json data;
        // get mongodb root password
        std::ifstream compose (config().get_backups_compose_path()+"/docker-compose.yml", std::ifstream::in);
        std::string line;

        std::string mongo_password;

        while(std::getline(compose,line,'\n')) {
            if (line.find("- MONGO_INITDB_ROOT_PASSWORD") != std::string::npos) {
                unsigned first_del = line.find('=');
                unsigned last_del = line.find('\n');
                mongo_password = line.substr(first_del+1, last_del - first_del-1);
            }
        }

        if (!Docker::Container::copy_to_container("mongodb", backup_folder+"/"+tag()+"/mongodbdump-"+tag()+".tar", "/")) {
            data["status"] = false;
            data["error"].push_back("Failed copying backup to mongodb container");
            return data;
        }
        if (!Docker::Container::exec("mongodb", "mongorestore /dump -u thinger -p "+mongo_password)) {
            data["status"] = false;
            data["error"].push_back("Failed restoring mongodb backup");
            return data;
        }

        data["status"] = true;

        return data;
    }

    json restore_influxdb() const {
        json data;

        std::string influxdb_version = Platform::Utils::InfluxDB::get_version();

        if (influxdb_version.starts_with("v2.")) {
            if (!Docker::Container::copy_to_container("influxdb2", backup_folder+"/"+tag()+"/influxdb2dump-"+tag()+".tar", "/")) {
                data["status"]  = false;
                data["error"].push_back("Failed copying backup to influxdb2 container");
                return data;
            }
            if (!Docker::Container::exec("influxdb2", "influx restore /dump --full")) {
                data["status"]  = false;
                data["error"].push_back("Failed restoring influxdb2 backup");
                return data;
            }
        } else if (influxdb_version.starts_with("1.")){
            if (!Docker::Container::copy_to_container("influxdb", backup_folder+"/"+tag()+"/influxdbdump-"+tag()+".tar", "/")) {
                data["status"]  = false;
                data["error"].push_back("Failed copying backup to influxdb container");
                return data;
            }
            if (!Docker::Container::exec("influxdb", "influxd restore --portable /dump")) {
                data["status"]  = false;
                data["error"].push_back("Failed restoring influxdb backup");
                return data;
            }
        }

        data["status"] = true;

        return data;
    }

    json restore_plugins() const {

        json data;

        // Executed after restore_thinger
        if (!std::filesystem::exists(config().get_backups_data_path()+"/thinger/users/")) {
            data["status"] = true;
            data["msg"].push_back("Platform has no users");
            return data;
        }

        for (const auto & p1 : fs::directory_iterator(config().get_backups_data_path()+"/thinger/users/")) { // users
            if (! std::filesystem::exists(p1.path().string()+"/plugins/")) continue;

            std::string user = p1.path().filename().string();

            // restore networks
            std::string network_id = Docker::Network::create_from_inspect(backup_folder+"/"+tag()+"/plugins/"+user+"-network.json");
            if (network_id.empty()) {
                data["error"].push_back("Failed restoring "+user+" docker network");
            } else {
                data["msg"].push_back("Restored "+user+" docker network");
            }

            // restore plugins
            for (const auto & p2 : fs::directory_iterator(p1.path().string()+"/plugins/")) { // plugins
                std::string container_name = user+"-"+p2.path().filename().string();
                if (Docker::Container::create_from_inspect(backup_folder+"/"+tag()+"/plugins/"+container_name+".json", network_id))
                    data["msg"].push_back("Restored "+container_name+" docker container");
                else
                    data["error"].push_back("Failed restoring "+container_name+" docker container");

                if (Docker::Container::start(container_name))
                    data["msg"].push_back("Started "+container_name+" docker containeer");
                else
                    data["error"].push_back("Failed starting "+container_name+" docker container");
            }
        }

        if (data.contains("error"))
            data["status"] = false;
        else
            data["status"] = true;

        return data;
    }

    bool clean_restore() const {
        bool status = true;
        status = status && std::filesystem::remove_all(backup_folder+"/"+file_to_download);
        status = status && std::filesystem::remove_all(backup_folder+"/"+tag());
        status = status && Docker::Container::exec("mongodb", "rm -rf /dump");
        if (std::filesystem::exists(config().get_backups_data_path()+"/influxdb2")) {
            status = status && Docker::Container::exec("influxdb2", "rm -rf /dump");
        } else {
            status = status && Docker::Container::exec("influxdb", "rm -rf /dump");
        }
        return status;
    }

    bool restart_platform() const {
        json data;
        if (!Docker::Container::restart("mongodb"))
            data["error"].push_back("Failed restaring mongodb container");
        if (!Docker::Container::restart("influxdb"))
            data["error"].push_back("Failed restarting influxdb container");
        if (!Docker::Container::restart("thinger"))
            data["error"].push_back("Failed restarting thinger container");

        if (data.contains("error"))
            data["status"] = false;
        else
            data["status"] = true;

        return data;
    }

};
