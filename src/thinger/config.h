#include <nlohmann/json.hpp>

#include <map>
#include <iostream>
#include <fstream>
#include <random>

#include <thinger_client.h>

using json = nlohmann::json;

constexpr std::string_view DF_CONFIG_PATH = "/etc/thinger_io/thinger_monitor.json";

namespace thinger::monitor::config {

    template< typename T >
    T get(json j, const json::json_pointer& p, T fallback) {

        try {
            return j.at(p);
        } catch (json::out_of_range const&) {
            return fallback;
        }
    }

}

namespace thinger::monitor::utils {

    std::string generate_credentials(std::size_t length) {

        const std::string CHARACTERS = "0123456789abcdefghijklmnopqrstuvwxyz!@#$%^&*()ABCDEFGHIJKLMNOPQRSTUVWXYZ";

        std::random_device random_device;
        std::mt19937 generator(random_device());
        std::uniform_int_distribution distribution(0, static_cast<int>(CHARACTERS.size()) - 1);

        std::string random_string;

        for (std::size_t i = 0; i < length; ++i) {
            random_string += CHARACTERS[distribution(generator)];
        }

        return random_string;
    }

    json to_json(pson_object const& p)  {

        json j;

        auto it = p.begin();

        if(!it.valid()) j = json({});

        while(it.valid()) {

            auto key = it.item().name();
            pson& value = it.item().value(); // auto returns a malloc_consolidate()

            /*
            if (value.is_empty()) {
                //if (value.is_object())
                //    j[key] = json({});
                //else if (value.is_array())
                //    j[key] = json::array();
                //j[key] = json::array();
                it.next();
                continue;
            }
            */

            if (value.is_object()) {
                j[key] = to_json(value); //TODO: remove recursion which could lead to stackoverflow
            } else if (value.is_array()) {
                pson_array const& array = value;
                auto it_arr = array.begin();
                j[key] = json::array();
                while (it_arr.valid()) {
                    std::string string = it_arr.item();
                    j[key].push_back(string);
                    it_arr.next();
                }
            }
            else if (value.is_boolean())  j[key] = value.get_value<bool>();
            else if (value.is_integer())  j[key] = value.get_value<int>();
            else if (value.is_float())    j[key] = value.get_value<float>();
            else if (value.is_number())   j[key] = value.get_value<double>();
            else if (value.is_string())   j[key] = (std::string)value;

            it.next();
        }

        return j;
    }

    pson to_pson(json const& j) {

        pson p;

        for (auto const& rs : j.items()) {
            auto key = rs.key().c_str();
            auto value = rs.value();
            // TODO: add rest of data types or use generic get function
            if (value.is_boolean())             p[key] = value.get<bool>();
            else if (value.is_number_integer()) p[key] = value.get<int>();
            else if (value.is_number_float())   p[key] = value.get<float>();
            else if (value.is_number())         p[key] = value.get<double>();
            else if (value.is_string())         p[key] = value.get<std::string>();
            else if (value.is_array()) {
                pson_array& array = p[key];
                for (auto& rs_val : value) {
                  array.add(rs_val.get<std::string>());
                }
            } else if (value.is_object()) {
                pson_object& obj = p[key];
                for (auto const& rs_obj : value.items()) {
                    obj[rs_obj.key().c_str()] = rs_obj.value().get<std::string>().c_str();
                    // TODO: implement numbers, bools and arrays inside objects
                }
            }
        }

        return p;
    }

}

namespace thinger::monitor {

    class Config {
    public:

        const std::vector<std::string> remote_properties = {"resources","backups","storage"};

        Config() : config_() {
            path_ = std::string(DF_CONFIG_PATH);
            load_config();
        }

        //Config(json config) : config_(config) {}

        explicit Config(std::string_view path) : config_(), path_(path) {
            load_config();
        }

        bool update(std::string const& property, pson& data) {

          if (json remote_config = utils::to_json(data); remote_config != config_[property]) {
                config_[property] = remote_config;
                save_config();

                return true;
            }
            return false;
          }

        /* Setters */
        void set_path(std::string_view path) {
            path_ = path;
            load_config();
        }

        void set_url(std::string_view url) {
            config_["server"]["url"] = url;
        }

        void set_user(std::string_view user) {
            config_["server"]["user"] = user;
        }

        void set_ssl(bool ssl) {
            config_["server"]["ssl"] = ssl;
        }

        void set_device() {
            // Check if device name exists, if not set it to hostname
            if (config_["device"]["id"].empty()) {
                std::string hostname;
                std::ifstream hostinfo ("/etc/hostname", std::ifstream::in);
                hostinfo >> hostname;
                if (config_["device"]["name"].empty())
                    config_["device"]["name"] = hostname;

                // TODO: When forcing C++20 replace for std::ranges::replace
                std::replace(hostname.begin(), hostname.end(),'.','_');
                std::replace(hostname.begin(), hostname.end(),'-','_');
                config_["device"]["id"] = hostname;
            }
            if (config_["device"]["credentials"].empty()) {
                config_["device"]["credentials"] = utils::generate_credentials(16);
            }
            save_config();
        }

        /* Getters */
        [[nodiscard]] std::string get_url() const {
          return config::get(config_, "/server/url"_json_pointer, std::string(""));
        }

        [[nodiscard]] std::string get_user() const {
            return config::get(config_, "/server/user"_json_pointer, std::string(""));
        }

        [[nodiscard]] std::string get_id() const {
            return config::get(config_, "/device/id"_json_pointer, std::string(""));
        }

        [[nodiscard]] std::string get_name() const {
            return config::get(config_, "/device/name"_json_pointer, std::string(""));
        }

        [[nodiscard]] std::string get_credentials() const {
            return config::get(config_, "/device/credentials"_json_pointer, std::string(""));
        }

        [[nodiscard]] bool get_ssl() const {
            return config::get(config_, "/server/ssl"_json_pointer, true);
        }

        [[nodiscard]] bool get_defaults() const {
            return config::get(config_, "/resources/defaults"_json_pointer, false);
        }

        [[nodiscard]] json get_filesystems() const {
            return config::get(config_, "/resources/filesystems"_json_pointer, json({}));
        }

        [[nodiscard]] json get_drives() const {
            return config::get(config_, "/resources/drives"_json_pointer, json({}));
        }

        [[nodiscard]] json get_interfaces() const {
          return config::get(config_, "/resources/interfaces"_json_pointer, json({}));
        }

        [[nodiscard]] std::string get_storage() const {
            return config::get(config_, "/backups/storage"_json_pointer, std::string(""));
        }

        [[nodiscard]] std::string get_data_path() const {
            return config::get(config_, "/backups/data_path"_json_pointer, std::string("/data"));
        }

        [[nodiscard]] std::string get_compose_path() const {
            return config::get(config_, "/backups/compose_path"_json_pointer, std::string("/"));
        }

        [[nodiscard]] std::string get_bucket(std::string const& st) const {
            auto jp = json::json_pointer("/storage/"+st+"/bucket");
            return config::get(config_, jp, std::string(""));
        }

        [[nodiscard]] std::string get_region(std::string const& st) const {
            auto jp = json::json_pointer("/storage/"+st+"/region");
            return config::get(config_, jp, std::string(""));
        }

        [[nodiscard]] std::string get_access_key(std::string const& st) const {
            auto jp = json::json_pointer("/storage/"+st+"/access_key");
            return config::get(config_, jp, std::string(""));
        }

        [[nodiscard]] std::string get_secret_key(std::string const& st) const {
            auto jp = json::json_pointer("/storage/"+st+"/secret_key");
            return config::get(config_, jp, std::string(""));
        }

        [[nodiscard]] std::string get_backup() const {
            return config::get(config_, "/backups/system"_json_pointer, std::string(""));
        }

        [[nodiscard]] std::string get_endpoints_token() const {
            return config::get(config_, "/backups/endpoints_token"_json_pointer, std::string(""));
        }

        [[nodiscard]] pson get(std::string const& property) const {

            pson p;

            // only remote properties in json
            // TODO: When forcing C++20 replace for std::ranges::replace
            if (std::find(remote_properties.begin(), remote_properties.end(), property) == remote_properties.end())
                return p;

            auto jp = json::json_pointer("/"+property);
            return utils::to_pson(config::get(config_, jp, json({})));
        }

    private:
        json config_;

        std::string path_;

        void load_config() {

            std::filesystem::path f(path_);

            if (std::filesystem::exists(f)) {
                std::ifstream config_file(path_);
                config_file >> config_;
            }
        }

        void save_config() const {

            if (std::filesystem::path f(path_); !std::filesystem::exists(f)) {
                std::filesystem::create_directories(f.parent_path());
            }

            std::ofstream file(path_);
            file << std::setw(2) << config_ << std::endl;
        }

    };
}
