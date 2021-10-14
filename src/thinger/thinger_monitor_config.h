#include <filesystem>
#include <jsoncpp/json/json.h>
#include <fstream>
#include <random>

#define DF_CONFIG_PATH "/etc/thinger_io/monitor/app.json"

namespace fs = std::filesystem;

using std::filesystem::current_path;

// This class only represents the same information contained in the configuration file,
// and the respective operations in order to configure read and save to it.
//
// Any changes in this object will persist into the file in order to maintain consistency.
class ThingerMonitorConfig {

public:

    ThingerMonitorConfig() : config_(), filesystems_(), drives_(), interfaces_(), defaults_() {
      config_path_ = DF_CONFIG_PATH;
      load_config();
    }

    ThingerMonitorConfig(
      std::vector<std::string> filesystems,
      std::vector<std::string> drives,
      std::vector<std::string> interfaces,
      bool defaults = true) :
        filesystems_(filesystems),
        drives_(drives),
        interfaces_(interfaces),
        defaults_(defaults)
    {
      config_path_ = DF_CONFIG_PATH;
      load_config();
    }

    bool update_with_remote(pson& data) {

        Json::Value remote_config = to_json_rs(data);

        if (remote_config.compare(config_["resources"]) != 0) {
            config_["resources"] = remote_config;
            save_config();

            return true;
        }

        return false;
    }

    pson in_pson() {
        // TODO: return full config in pson?
        Json::Value config = config_;

        pson data;
        for (auto rs : config_["resources"].getMemberNames()) {
            if (config["resources"][rs].isBool()) data[rs.c_str()] = config["resources"][rs].asBool();
            else {
                pson_array& array = data[rs.c_str()];
                for (auto rs_val : config["resources"][rs]) {
                    array.add(rs_val.asString());
                }
            }
        }

        return data;
    }

    //------------------//
    //----- Has's -----//
    //------------------//

    bool has_user() {
        return config_.isMember("user");
    }

    bool has_device() {
        return config_.isMember("device") && has_device_id() && has_device_credentials();
    }

    bool has_device_id() {
        return config_.isMember("device") && config_["device"].isMember("id");
    }

    bool has_device_credentials() {
        return config_.isMember("device") && config_["device"].isMember("credentials");
    }

    bool has_filesystems() {
        return config_.isMember("resources") && config_["resources"].isMember("filesystems");
    }

    bool has_drives() {
        return config_.isMember("resources") && config_["resources"].isMember("drives");
    }

    bool has_interfaces() {
        return config_.isMember("resources") && config_["resources"].isMember("interfaces");
    }

    bool has_defaults() {
        return config_.isMember("resources") && config_["resources"].isMember("defaults");
    }

    //-------------------//
    //----- Setters -----//
    //-------------------//

    void set_config_path(const std::string& config_path) {
        config_path_ = config_path;
        // if file exists merge and load, if not create it and write it
        std::filesystem::path f(config_path_);
        if (std::filesystem::exists(f)) {
            if (config_.empty()) {
                load_config();
            } else {
                Json::Value config = config_;
                load_config();
                merge_json(config_, config);
                save_config();
            }
        }
    }

    void set_user(std::string user) {
        config_["user"] = user;
        save_config();
    }

    void set_device() {
        // Check if device name exists, if not set it to hostname
        if (!has_device_id()) {
            std::string hostname;
            std::ifstream hostinfo ("/etc/hostname", std::ifstream::in);
            hostinfo >> hostname;

            config_["device"]["id"] = hostname;
        }
        if (!has_device_credentials()) {
            config_["device"]["credentials"] = generate_credentials(16);
        }
        save_config();
    }

    void set_device(std::string id) {
        config_["device"]["id"] = id;
        config_["device"]["credentials"] = generate_credentials(16);
        save_config();
    }

    void set_device_credentials() {
        config_["device"]["credentials"] = generate_credentials(16);
        save_config();
    }

    //-------------------//
    //----- Getters -----//
    //-------------------//

    Json::Value get_config() const {
        return config_;
    }

    std::string get_user() const {
        return config_["user"].asString();
    }

    std::string get_device_id() const {
        return config_["device"]["id"].asString();
    }

    std::string get_device_credentials() const {
        return config_["device"]["credentials"].asString();
    }

    std::vector<std::string> get_filesystems() {

        std::vector<std::string> filesystems;

        if (has_filesystems()) {
            for (auto fs : config_["resources"]["filesystems"]) {
                filesystems.push_back(fs.asString());
            }
        }
        return filesystems;
    }

    std::vector<std::string> get_drives() {

        std::vector<std::string> drives;

        if (has_drives()) {
            for (auto dv : config_["resources"]["drives"]) {
                drives.push_back(dv.asString());
            }
        }
        return drives;
    }

    std::vector<std::string> get_interfaces() {

        std::vector<std::string> interfaces;

        if (has_interfaces()) {
            for (auto ifc : config_["resources"]["interfaces"]) {
                interfaces.push_back(ifc.asString());
            }
        }
        return interfaces;
    }

    bool get_defaults() {
        return (has_defaults()) ? config_["resources"]["defaults"].asBool() : true;
    }

protected:
    Json::Value config_;
    std::string config_path_;

    std::vector<std::string> filesystems_;
    std::vector<std::string> drives_;
    std::vector<std::string> interfaces_;
    bool defaults_;

private:

    void load_config(){
        Json::Value config;

        std::filesystem::path f(config_path_);

        if (std::filesystem::exists(f)) {
            std::ifstream config_file(config_path_, std::ifstream::binary);
            config_file >> config_;
        }

    }

    void save_config() {
        std::filesystem::path f(config_path_);

        if (!std::filesystem::exists(f)) {
            std::filesystem::create_directories(f.parent_path());
        }

        Json::StreamWriterBuilder wbuilder;
        wbuilder["indentation"] = "  ";
        std::unique_ptr<Json::StreamWriter> writer(wbuilder.newStreamWriter());
        std::ofstream file(config_path_);
        writer->write(config_, &file);
    }

    std::string generate_credentials(std::size_t length) {

        std::string CHARACTERS = "0123456789abcdefghijklmnopqrstuvwxyz!@#$%^&*()ABCDEFGHIJKLMNOPQRSTUVWXYZ";

        std::random_device random_device;
        std::mt19937 generator(random_device());
        std::uniform_int_distribution<> distribution(0, CHARACTERS.size() - 1);

        std::string random_string;

        for (std::size_t i = 0; i < length; ++i) {
            random_string += CHARACTERS[distribution(generator)];
        }

        return random_string;

    }

    Json::Value to_json_rs(pson& data) {

        Json::Value config;

        std::vector<std::string> resources = {"defaults", "interfaces", "filesystems", "drives"};

        for (auto rs : resources) {
            if (data[rs.c_str()].is_empty()) continue;

            if (data[rs.c_str()].is_boolean()) config[rs] = (bool)data[rs.c_str()];
            if (data[rs.c_str()].is_array()) {
                pson_array& array = data[rs.c_str()];
                pson_container<pson>::iterator it = array.begin();
                while (it.valid()) {
                    std::string string = it.item();
                    config[rs].append(string);
                    it.next();
                }
            }
        }

        return config;
    }

    void merge_json(Json::Value& a, Json::Value& b) {
        if (!a.isObject() || !b.isObject()) return;

        for (const auto& key : b.getMemberNames()) {
            if (a[key].isObject()) {
                merge_json(a[key], b[key]);
            } else {
                a[key] = b[key];
            }
        }
    }

};
