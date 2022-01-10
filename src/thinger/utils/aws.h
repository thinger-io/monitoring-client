#pragma once

#include <chrono>

#include <httplib.h>

#include "date.h"
#include "base64.h"

#include <openssl/sha.h>
#include <openssl/hmac.h>

class AWS {

public:

    static int upload_to_s3(const std::string file_path, const std::string bucket, const std::string region, const std::string access_key, const std::string secret_key) {

        const std::string content_type = "application/x-compressed-tar";

        httplib::Client cli("https://"+bucket+".s3-"+region+".amazonaws.com");

        auto date = Date::now_rfc5322();
        std::string filename = std::filesystem::path(file_path).filename();

        const std::string signature_string = "PUT\n\n"+content_type+","+content_type+"\n"+date+"\n/"+bucket+"/"+filename;

        const std::string signature_hash = hmac_sha1(secret_key, signature_string);

        httplib::Headers headers = {
            { "Host", bucket+".s3.amazonaws.com" },
            { "Date", date}, // Ex: Mon, 03 Jan 2022 12:46:34 +0100
            { "Content-Type", content_type},
            { "Authorization", "AWS "+access_key+":"+signature_hash}
        };

        // Read file
        // TODO: send content with content provider
        std::ifstream file(file_path);
        std::stringstream body;
        body << file.rdbuf();

        // PUT(path, headers, std::string body, const char content_type)
        std::cout << "[___AWS] Uploading file "+filename+" to "+bucket+" bucket" << std::endl;
        auto res = cli.Put(("/"+filename).c_str(), headers, body.str(), content_type.c_str());

        return res->status;

    }

    static int download_from_s3(const std::string file_path, const std::string bucket, const std::string region, const std::string access_key, const std::string secret_key) {

        const std::string content_type = "application/x-compressed-tar";

        httplib::Client cli("https://"+bucket+".s3-"+region+".amazonaws.com");

        auto date = Date::now_rfc5322();
        std::string filename = std::filesystem::path(file_path).filename();

        const std::string signature_string = "GET\n\n"+content_type+"\n"+date+"\n/"+bucket+"/"+filename;

        const std::string signature_hash = hmac_sha1(secret_key, signature_string);

        httplib::Headers headers = {
            { "Host", bucket+".s3.amazonaws.com" },
            { "Date", date}, // Ex: Mon, 03 Jan 2022 12:46:34 +0100
            { "Content-Type", content_type},
            { "Authorization", "AWS "+access_key+":"+signature_hash}
        };

        std::ofstream file(file_path);

        // PUT(path, headers, std::string body, const char content_type)
        std::cout << "[___AWS] Downloading file "+filename+" from "+bucket+" bucket" << std::endl;
        cli.set_default_headers(headers);
        auto res = cli.Get(("/"+filename).c_str(),
          [&](const char *data, size_t data_length) {
            file.write(data, data_length);
            return true;
          });

        return res->status;
        return 0;
    }

private:

    static std::string hmac_sha1(std::string key, std::string msg) {

        unsigned char hash[32];

        HMAC_CTX *hmac = HMAC_CTX_new();
        HMAC_Init_ex(hmac, &key[0], key.length(), EVP_sha1(), NULL);
        HMAC_Update(hmac, (unsigned char*)&msg[0], msg.length());
        unsigned int len = 32;
        HMAC_Final(hmac, hash, &len);
        HMAC_CTX_free(hmac);

        /* HEX
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < len; i++) {
            ss << std::hex << std::setw(2)  << (unsigned int)hash[i];
        }
        */

        std::stringstream ss;
        ss << std::setfill('0');
        for (int i = 0; i < len; i++) {
            ss  << hash[i];
        }


        return (base64_encode(ss.str()));

    }
};
