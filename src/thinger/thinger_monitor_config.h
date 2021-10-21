#include <filesystem>
#include <fstream>
#include <random>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace fs = std::filesystem;

#define DF_CONFIG_PATH "/etc/thinger_io/monitor/app.json"

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

        json remote_config = to_json_rs(data);

        if (remote_config != config_["resources"]) {
            config_["resources"] = remote_config;
            save_config();

            return true;
        }

        return false;
    }

    pson in_pson() {
        // TODO: return full config in pson?

        pson data;
        for (auto& rs : config_["resources"].items()) {
            if (rs.value().is_boolean()) data[rs.key().c_str()] = rs.value().get<bool>();
            else {
                pson_array& array = data[rs.key().c_str()];
                for (auto& rs_val : rs.value()) {
                    array.add(rs_val.get<std::string>());
                }
            }
        }

        return data;
    }

    //------------------//
    //----- Has's -----//
    //------------------//

    bool has_user() {
        return config_.contains("user");
    }

    bool has_device() {
        return config_.contains("device") && has_device_id() && has_device_credentials();
    }

    bool has_device_id() {
        return config_.contains("device") && config_["device"].contains("id");
    }

    bool has_device_credentials() {
        return config_.contains("device") && config_["device"].contains("credentials");
    }

    bool has_filesystems() {
        return config_.contains("resources") && config_["resources"].contains("filesystems");
    }

    bool has_drives() {
        return config_.contains("resources") && config_["resources"].contains("drives");
    }

    bool has_interfaces() {
        return config_.contains("resources") && config_["resources"].contains("interfaces");
    }

    bool has_defaults() {
        return config_.contains("resources") && config_["resources"].contains("defaults");
    }

    bool has_server() {
        return config_.contains("server");
    }

    bool has_server_url() {
        return has_server() && config_["server"].contains("url");
    }

    bool has_server_secure() {
        return has_server() && config_["server"].contains("secure");
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
                json config = config_;
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
            std::replace(hostname.begin(), hostname.end(),'.','_');
            std::replace(hostname.begin(), hostname.end(),'-','_');

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

    void set_server_url(std::string url) {
        config_["server"]["url"] = url;
        save_config();
    }

    void set_server_secure(bool secure) {
        config_["server"]["secure"] = secure;
        save_config();
    }

    //-------------------//
    //----- Getters -----//
    //-------------------//

    json get_config() const {
        return config_;
    }

    std::string get_user() const {
        return config_["user"].get<std::string>();
    }

    std::string get_device_id() const {
        return config_["device"]["id"].get<std::string>();
    }

    std::string get_device_credentials() const {
        return config_["device"]["credentials"].get<std::string>();
    }

    std::vector<std::string> get_filesystems() {

        if (has_filesystems()) {
            for (auto fs : config_["resources"]["filesystems"]) {
                filesystems_.push_back(fs.get<std::string>());
            }
        }
        return filesystems_;
    }

    std::vector<std::string> get_drives() {

        if (has_drives()) {
            for (auto dv : config_["resources"]["drives"]) {
                drives_.push_back(dv.get<std::string>());
            }
        }
        return drives_;
    }

    std::vector<std::string> get_interfaces() {

        if (has_interfaces()) {
            for (auto ifc : config_["resources"]["interfaces"]) {
                interfaces_.push_back(ifc.get<std::string>());
            }
        }
        return interfaces_;
    }

    bool get_defaults() {
        return (has_defaults()) ? config_["resources"]["defaults"].get<bool>() : true;
    }

    std::string get_server_url() {
        return (has_server_url()) ? config_["server"]["url"].get<std::string>() : THINGER_SERVER;
    }

    bool get_server_secure() {
        return (has_server_secure()) ? config_["server"]["secure"].get<bool>() : true;
    }

protected:
    json config_;
    std::string config_path_;

    std::vector<std::string> filesystems_;
    std::vector<std::string> drives_;
    std::vector<std::string> interfaces_;
    bool defaults_;

private:

    void load_config(){

        std::filesystem::path f(config_path_);

        if (std::filesystem::exists(f)) {
            std::ifstream config_file(config_path_);
            config_file >> config_;
        }

    }

    void save_config() {
        std::filesystem::path f(config_path_);

        if (!std::filesystem::exists(f)) {
            std::filesystem::create_directories(f.parent_path());
        }

        std::ofstream file(config_path_);
        file << std::setw(2) << config_ << std::endl;
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

    json to_json_rs(pson& data) {

        json config;

        std::vector<std::string> resources = {"defaults", "interfaces", "filesystems", "drives"};

        for (auto rs : resources) {
            if (data[rs.c_str()].is_empty()) continue;

            if (data[rs.c_str()].is_boolean()) config[rs] = (bool)data[rs.c_str()];
            if (data[rs.c_str()].is_array()) {
                pson_array& array = data[rs.c_str()];
                pson_container<pson>::iterator it = array.begin();
                while (it.valid()) {
                    std::string string = it.item();
                    //config[rs].append(string);
                    config[rs].push_back(string);
                    it.next();
                }
            }
        }

        return config;
    }

    void merge_json(json& a, json& b) {
        if (!a.is_object() || !b.is_object()) return;

        for (auto& [key, value] : b.items()) {
            if (a[key].is_object()) {
                merge_json(a[key], b[key]);
            } else {
                a[key] = value;
            }
        }
    }

};
