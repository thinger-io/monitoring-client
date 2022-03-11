#pragma once

#include <chrono>
#include <filesystem>

#include <httplib.h>

#include "date.h"
//#include "base64.h"
#include "crypto.h"
#include "xml.h"

#include "aws/s3.h"

#include <nlohmann/json.hpp>

//#define CPPHTTPLIB_SEND_FLAGS 0

namespace AWS {

    int multipart_upload_to_s3(const std::string& file_path, std::string& bucket, std::string& region, std::string& access_key, std::string& secret_key) {

        S3::MultipartUpload mpu = S3::MultipartUpload(bucket, region, access_key, secret_key);

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[____AWS] Inititiating upload of "+file_path << std::endl;
        std::string upload_id = mpu.initiate_upload(file_path);
        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[____AWS] Upload initiated with uploadid "+upload_id << std::endl;
        return mpu.upload(upload_id, file_path);

    }

    int upload_to_s3(const std::string file_path, const std::string bucket, const std::string region, const std::string access_key, const std::string secret_key) {

        const std::string content_type = "application/x-compressed-tar";

        httplib::Client cli("https://"+bucket+".s3-"+region+".amazonaws.com");
        cli.set_write_timeout(120, 0); // 5 seconds

        Date date = Date();
        auto date_string = date.to_rfc5322();
        std::string filename = std::filesystem::path(file_path).filename();

        const std::string signature_string = "PUT\n\n"+content_type+","+content_type+"\n"+date_string+"\n/"+bucket+"/"+filename;
        const std::string signature_hash = Crypto::base64::encode(Crypto::hash::hmac_sha1(secret_key, signature_string));

        httplib::Headers headers = {
            { "Host", bucket+".s3.amazonaws.com" },
            { "Date", date_string}, // Ex: Mon, 03 Jan 2022 12:46:34 +0100
            { "Content-Type", content_type},
            { "Authorization", "AWS "+access_key+":"+signature_hash}
        };

        std::ifstream file(file_path);

        // Without content provider, for big files send by multipart
        std::stringstream body;
        body << file.rdbuf();

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[____AWS] Uploading file "+filename+" to "+bucket+" bucket" << std::endl;
        auto res = cli.Put(("/"+filename).c_str(), headers, body.str(), content_type.c_str());

        return res->status;

    }

    int download_from_s3(const std::string file_path, const std::string bucket, const std::string region, const std::string access_key, const std::string secret_key) {

        const std::string content_type = "application/x-compressed-tar";

        httplib::Client cli("https://"+bucket+".s3-"+region+".amazonaws.com");

        Date date = Date();
        auto date_string = date.to_rfc5322();
        std::string filename = std::filesystem::path(file_path).filename();

        const std::string signature_string = "GET\n\n"+content_type+"\n"+date_string+"\n/"+bucket+"/"+filename;

        const std::string signature_hash = Crypto::base64::encode(Crypto::hash::hmac_sha1(secret_key, signature_string));

        httplib::Headers headers = {
            { "Host", bucket+".s3.amazonaws.com" },
            { "Date", date_string}, // Ex: Mon, 03 Jan 2022 12:46:34 +0100
            { "Content-Type", content_type},
            { "Authorization", "AWS "+access_key+":"+signature_hash}
        };

        std::ofstream file(file_path);

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[____AWS] Downloading file "+filename+" from "+bucket+" bucket" << std::endl;

        cli.set_default_headers(headers);
        auto res = cli.Get(("/"+filename).c_str(),
          [&](const char *data, size_t data_length) {
            file.write(data, data_length);
            return true;
          });

        return res->status;
    }

};

