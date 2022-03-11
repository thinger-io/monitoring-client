#include <filesystem>

#include <nlohmann/json.hpp>

#include "crypto.h"

using json = nlohmann::json;

class JWT {

public:
    static json get_payload(std::string const& jwt) {
        // get in between two dots
        std::string base64_payload = get_payload_substring(jwt);
        std::string string_payload = Crypto::base64::decode(base64_payload);
        return json::parse(string_payload);

    }

private:
    static std::string get_payload_substring(std::string const& jwt) {
        unsigned first_del = jwt.find('.');
        unsigned last_del = jwt.find_last_of('.');
        return jwt.substr(first_del+1, last_del - first_del - 1);
    }

};
