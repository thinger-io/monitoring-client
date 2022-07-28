#include <filesystem>
#include <fstream>
#include <random>
#include <regex>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace fs = std::filesystem;

constexpr std::string_view DF_CONFIG_PATH = "/etc/thinger_io/thinger_monitor.json";

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
      const std::vector<std::string>& filesystems,
      const std::vector<std::string>& drives,
      const std::vector<std::string>& interfaces,
      bool defaults = true) :
        filesystems_(filesystems),
        drives_(drives),
        interfaces_(interfaces),
        defaults_(defaults)
    {
      config_path_ = DF_CONFIG_PATH;
      load_config();
    }

    bool update_with_remote(std::string& property, pson& data) {

        json remote_config = to_json(data);

        if (remote_config != config_[property]) {
            config_[property] = remote_config;
            save_config();

            return true;
        }

        return false;
    }

    pson in_pson(std::string& property) {

        pson data;
        if (!config_.contains(property)) {

            pson_object& obj = data[property.c_str()];
            config_[property] = json({{property, json({})}});
            //TODO: use this when get_property does not hang connection
            //pson_object& obj = data;
            //config_[property] = json({});
            save_config();
            return data;
        }

        for (auto& rs : config_[property].items()) {
            // TODO: add rest of data types
            if (rs.value().is_boolean()) data[rs.key().c_str()] = rs.value().get<bool>();
            else if (rs.value().is_string()) data[rs.key().c_str()] = rs.value().get<std::string>();
            else if (rs.value().is_array()) {
                pson_array& array = data[rs.key().c_str()];
                for (auto& rs_val : rs.value()) {
                    array.add(rs_val.get<std::string>());
                }
            } else if (rs.value().is_object()) {
                pson_object& obj = data[rs.key().c_str()];
                for (auto& rs_obj : rs.value().items()) {
                    obj[rs_obj.key().c_str()] = rs_obj.value().get<std::string>().c_str();
                }
            }
        }

        return data;
    }

    //------------------//
    //----- Has's -----//
    //------------------//

    bool has_user() const {
        return config_.contains("user") && !is_placeholder(get_user());
    }

    bool has_device() const {
        return config_.contains("device");
    }

    bool has_device_id() const {
        return has_device() && config_["device"].contains("id") && !is_placeholder(get_device_id());
    }

    bool has_device_name() const {
        return has_device() && config_["device"].contains("name") && !is_placeholder(get_device_name());
    }

    bool has_device_credentials() const {
        return has_device() && config_["device"].contains("credentials") && !is_placeholder(get_device_credentials());
    }

    bool has_filesystems() const {
        return config_.contains("resources") && config_["resources"].contains("filesystems");
    }

    bool has_drives() const {
        return config_.contains("resources") && config_["resources"].contains("drives");
    }

    bool has_interfaces() const {
        return config_.contains("resources") && config_["resources"].contains("interfaces");
    }

    bool has_defaults() const {
        return config_.contains("resources") && config_["resources"].contains("defaults");
    }

    bool has_server() const {
        return config_.contains("server");
    }

    bool has_server_url() const {
        return has_server() && config_["server"].contains("url") && !is_placeholder(config_["server"]["url"].get<std::string>());
    }

    bool has_server_secure() const {
        return has_server() && config_["server"].contains("secure");
    }

    bool has_backups() const {
        return config_.contains("backups");
    }

    bool has_backups_system() const {
        return has_backups() && config_["backups"].contains("system");
    }

    bool has_backups_storage() const {
        return has_backups() && config_["backups"].contains("storage");
    }

    bool has_backups_data_path() const {
        return has_backups() && config_["backups"].contains("data_path");
    }

    bool has_backups_compose_path() const {
        return has_backups() && config_["backups"].contains("compose_path");
    }

    //bool has_backups_clean() {
    //    return has_backups() && config_["backups"].contains("clean");
    //}

    bool has_backups_endpoints_token() const {
        return has_backups() && config_["backups"].contains("endpoints_token");
    }

    bool has_storage_bucket(const std::string& storage) const {
        return has_backups() && config_["storage"][storage].contains("bucket");
    }

    bool has_storage_region(const std::string& storage) const {
        return has_backups() && config_["storage"][storage].contains("region");
    }

    bool has_storage_access_key(const std::string& storage) const {
        return has_backups() && config_["storage"][storage].contains("access_key");
    }

    bool has_storage_secret_key(const std::string& storage) const {
        return has_backups() && config_["storage"][storage].contains("secret_key");
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
            if (!has_device_name())
                config_["device"]["name"] = hostname;

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

    std::string get_device_name() const {
        return config_["device"]["name"].get<std::string>();
    }

    std::string get_device_credentials() const {
        return config_["device"]["credentials"].get<std::string>();
    }

    std::vector<std::string> get_filesystems() {

        if (has_filesystems()) {
            filesystems_.clear();
            for (auto fs : config_["resources"]["filesystems"]) {
                filesystems_.push_back(fs.get<std::string>());
            }
        }
        return filesystems_;
    }

    std::vector<std::string> get_drives() {

        if (has_drives()) {
            drives_.clear();
            for (auto dv : config_["resources"]["drives"]) {
                drives_.push_back(dv.get<std::string>());
            }
        }
        return drives_;
    }

    std::vector<std::string> get_interfaces() {

        if (has_interfaces()) {
            interfaces_.clear();
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

    // -- BACKUPS -- //
    std::string get_backups_system() {
        return (has_backups_system()) ? config_["backups"]["system"].get<std::string>() : "";
    }

    std::string get_backups_storage() {
        return (has_backups_storage()) ? config_["backups"]["storage"].get<std::string>() : "";
    }

    std::string get_backups_data_path() {
        return (has_backups_data_path()) ? config_["backups"]["data_path"].get<std::string>() : "/data";
    }

    std::string get_backups_compose_path() {
        return (has_backups_compose_path()) ? config_["backups"]["compose_path"].get<std::string>() : "/";
    }

    std::string get_backups_endpoints_token() {
        return (has_backups_endpoints_token()) ? config_["backups"]["endpoints_token"].get<std::string>() : "";
    }

    //bool get_backups_clean() {
    //    return (has_backups_clean()) ? config_["backups"]["clean"].get<bool>() : true;
    //}

    std::string get_storage_bucket(const std::string& storage) {
        return (has_storage_bucket(storage)) ? config_["storage"][storage]["bucket"].get<std::string>() : "";
    }

    std::string get_storage_region(const std::string& storage) {
        return (has_storage_region(storage)) ? config_["storage"][storage]["region"].get<std::string>() : "";
    }

    std::string get_storage_access_key(const std::string& storage) {
        return (has_storage_access_key(storage)) ? config_["storage"][storage]["access_key"].get<std::string>() : "";
    }

    std::string get_storage_secret_key(const std::string& storage) {
        return (has_storage_secret_key(storage)) ? config_["storage"][storage]["secret_key"].get<std::string>() : "";
    }

    void reload_config() {
        load_config();
    }

private:

    json config_;
    std::string config_path_;

    std::vector<std::string> filesystems_;
    std::vector<std::string> drives_;
    std::vector<std::string> interfaces_;
    bool defaults_;


    void load_config(){

        std::filesystem::path f(config_path_);

        if (std::filesystem::exists(f)) {
            std::ifstream config_file(config_path_);
            config_file >> config_;
        }

    }

    void save_config() const {
        std::filesystem::path f(config_path_);

        if (!std::filesystem::exists(f)) {
            std::filesystem::create_directories(f.parent_path());
        }

        std::ofstream file(config_path_);
        file << std::setw(2) << config_ << std::endl;
    }

    std::string generate_credentials(std::size_t length) const {

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

    json to_json(pson_object& data) const {

        json config;

        pson_container<pson_pair>::iterator it = data.begin();

        if (!it.valid()) config = json({});

        while(it.valid()) {
            if (it.item().value().is_empty()) {
                it.next();
                continue;
            }

            if (it.item().value().is_object()) {
                config[it.item().name()] = to_json(it.item().value());
            } else if (it.item().value().is_array()) {
                pson_array& array = it.item().value();
                pson_container<pson>::iterator it_arr = array.begin();
                while (it_arr.valid()) {
                    std::string string = it_arr.item();
                    config[it.item().name()].push_back(string);
                    it_arr.next();
                }
            } else if (it.item().value().is_boolean()) config[it.item().name()] = (bool)it.item().value();
            else if (it.item().value().is_string()) config[it.item().name()] = (std::string)it.item().value();

            it.next();
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

    bool is_placeholder(const std::string& value) const {
        return std::regex_match(value, std::regex("(<.*>)"));
    }

};
