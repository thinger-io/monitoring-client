#pragma once

#include <chrono>
#include <filesystem>

#include <httplib.h>

#include "date.h"
//#include "base64.h"
#include "crypto.h"
#include "xml.h"

#include <nlohmann/json.hpp>

//#define CPPHTTPLIB_SEND_FLAGS 0

namespace AWS {

    namespace {

        std::string aws_v4_key(const std::string secret_key, const std::string region) {
            const std::string kDate = Crypto::hash::hmac_sha256("AWS4"+secret_key, Date::now_iso8601('\0'));
            const std::string kRegion = Crypto::hash::hmac_sha256(kDate, region);
            const std::string kService = Crypto::hash::hmac_sha256(kRegion, "s3");
            const std::string kSigning = Crypto::hash::hmac_sha256(kService, "aws4_request");

            return kSigning;
        }

        std::string aws_v4_canonical_request(
          const std::string method,
          const std::string path,
          const std::string query_parameters,
          const std::string bucket,
          const std::string payload_hash,
          const std::string long_date)
        {

            const std::string canonical_request =
                method+"\n"+
                path+"\n"+
                query_parameters+"\n"+
                "host:"+bucket+".s3.amazonaws.com\n"+
                "x-amz-content-sha256:"+payload_hash+"\n"+
                "x-amz-date:"+long_date+"\n\n"+
                "host;x-amz-content-sha256;x-amz-date\n"+
                payload_hash;

            return canonical_request;
        }

        // TODO: date should not be short or long, but an object from which I can get whatever neccesary format
        std::string aws_v4_string_to_sign(const std::string date_long, const std::string date_short, const std::string region, const std::string canonical_request) {

            const std::string string_to_sign =
                std::string("AWS4-HMAC-SHA256\n")+
                date_long+"\n"+
                date_short+"/"+region+"/s3/aws4_request\n"+
                Crypto::hash::sha256(canonical_request);

            return string_to_sign;
        }

        std::string aws_v4_auth_header(const std::string access_key, const std::string date_short, const std::string region, const std::string signed_string) {

            const std::string auth_header = std::string("AWS4-HMAC-SHA256 ")+
                "Credential="+access_key+"/"+date_short+"/"+region+"/s3/aws4_request,"+
                "SignedHeaders=host;x-amz-content-sha256;x-amz-date,"+
                "Signature="+signed_string;

            return auth_header;
        }

        struct MPUPart {
            unsigned int part_id;
            std::string ETag;
        };

        std::string generate_xml_complete_mpu(MPUPart *parts_list, unsigned int parts) {

            std::string xml_res = "<CompleteMultipartUpload>\n";
            for (int i=0; i < parts; i++) {
                xml_res += "<Part>\n<PartNumber>"+std::to_string(parts_list[i].part_id)+"</PartNumber>\n";
                xml_res += "<ETag>"+parts_list[i].ETag+"</ETag>\n</Part>";
            }
            xml_res += "\n</CompleteMultipartUpload>";

            return xml_res;
        }

    }


    int multipart_upload_to_s3(const std::string file_path, const std::string bucket, const std::string region, const std::string access_key, const std::string secret_key) {

        const std::string content_type = "application/x-compressed-tar";

        httplib::Client cli("https://"+bucket+".s3-"+region+".amazonaws.com");
        cli.set_write_timeout(120, 0); // 120 seconds
        cli.set_keep_alive(true);

        // 1. Initiate Multipart upload
        //  Initiates and returns and upload ID
        auto date = Date::now_rfc5322();
        std::string filename = std::filesystem::path(file_path).filename();

        std::string amz_date = Date::now_utc_iso8601('\0',true);

        //const std::string empty_str_hash = Crypto::hash::sha256("");
        const std::string key = aws_v4_key(secret_key, region);

        std::string payload_hash = Crypto::hash::sha256("");
        std::string canonical_request = aws_v4_canonical_request("POST", "/"+filename,"uploads=",bucket, payload_hash, amz_date);
        std::string string_to_sign = aws_v4_string_to_sign(amz_date, Date::now_iso8601('\0'), region, canonical_request);
        std::string signed_string = Crypto::to_hex(Crypto::hash::hmac_sha256(key, string_to_sign));
        std::string auth_header = aws_v4_auth_header(access_key, Date::now_iso8601('\0'), region, signed_string);

        httplib::Headers headers = {
            { "Host", bucket+".s3.amazonaws.com" },
            {"x-amz-content-sha256", payload_hash}, // hash of empty string//,
            {"x-amz-date", amz_date},
            {"Authorization", auth_header}
        };
        cli.set_default_headers(headers);

        auto res = cli.Post(("/"+filename+"?uploads").c_str());
        if ( res.error() != httplib::Error::Success || res->status != 200 ) {
            throw "Failed Multipart upload";
            return res->status;
        }

        // TODO: treat error: std::cout << res.error() << std::endl;

        const std::string upload_id = XML::get_element_value(res->body, "UploadId");

        // TODO: abort multipart upload: should this file be a class?
        // 2. Upload part
        size_t buffer_size = 10<<20; // 10 Megabyte -> Up to 100GiB
        char *buffer = new char[buffer_size];
        std::ifstream file(file_path);

        auto file_size = std::filesystem::file_size(file_path);
        auto parts = file_size / buffer_size;
        auto last_part_size = file_size % buffer_size;
        MPUPart parts_list[parts+1];
        if (last_part_size != 0) parts = parts+1;

        for (int i = 1; i < parts; i++) { // send part by part
            file.read(buffer, buffer_size);

            std::cout << std::fixed << Date::millis()/1000.0 << " ";
            std::cout << "[____AWS] Multipart upload of "+filename+"; Uploading part "+std::to_string(i)+" out of "+std::to_string(parts) << std::endl;

            payload_hash = Crypto::hash::sha256(buffer, buffer_size);
            amz_date = Date::now_utc_iso8601('\0',true);

            canonical_request = aws_v4_canonical_request("PUT","/"+filename,
                "partNumber="+std::to_string(i)+"&uploadId="+upload_id,bucket,payload_hash,amz_date);
            string_to_sign = aws_v4_string_to_sign(amz_date, Date::now_iso8601('\0'), region, canonical_request);
            signed_string = Crypto::to_hex(Crypto::hash::hmac_sha256(key, string_to_sign));
            auth_header = aws_v4_auth_header(access_key, Date::now_iso8601('\0'), region, signed_string);

            headers = {
                { "Host", bucket+".s3.amazonaws.com" },
                { "x-amz-content-sha256", payload_hash },
                { "x-amz-date", amz_date },
                { "Authorization", auth_header }
            };
            cli.set_default_headers(headers);

            res = cli.Put(("/"+filename+"?partNumber="+std::to_string(i)+"&uploadId="+upload_id).c_str(),buffer,buffer_size,content_type.c_str());

            parts_list[i-1] = MPUPart(i, res->get_header_value("ETag"));

            if ( res.error() != httplib::Error::Success || res->status != 200 ) {
                throw "Failed Multipart upload";
                return res->status;
            }
        }

        // 3. Last part
        file.read(buffer, buffer_size);

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[____AWS] Multipart upload of "+filename+"; Uploading part "+std::to_string(parts)+" out of "+std::to_string(parts) << std::endl;

        payload_hash = Crypto::hash::sha256(buffer, last_part_size);
        amz_date = Date::now_utc_iso8601('\0',true);

        canonical_request = aws_v4_canonical_request("PUT","/"+filename,
            "partNumber="+std::to_string(parts)+"&uploadId="+upload_id,bucket,payload_hash,amz_date);
        string_to_sign = aws_v4_string_to_sign(amz_date, Date::now_iso8601('\0'), region, canonical_request);
        signed_string = Crypto::to_hex(Crypto::hash::hmac_sha256(key, string_to_sign));
        auth_header = aws_v4_auth_header(access_key, Date::now_iso8601('\0'), region, signed_string);

        headers = {
            { "Host", bucket+".s3.amazonaws.com" },
            { "x-amz-content-sha256", payload_hash },
            { "x-amz-date", amz_date },
            { "Authorization", auth_header }
        };
        cli.set_default_headers(headers);
        res = cli.Put(("/"+filename+"?partNumber="+std::to_string(parts)+"&uploadId="+upload_id).c_str(),buffer,last_part_size,content_type.c_str());
        if ( res.error() != httplib::Error::Success || res->status != 200 ) {
            throw "Failed Multipart upload";
            return res->status;
        }
        parts_list[parts-1] = MPUPart(parts, res->get_header_value("ETag"));

        // 4. Complete MPU
        std::string complete_xml = generate_xml_complete_mpu(parts_list, parts);
        payload_hash = Crypto::hash::sha256(complete_xml);
        amz_date = Date::now_utc_iso8601('\0',true);

        canonical_request = aws_v4_canonical_request("POST","/"+filename,"uploadId="+upload_id,bucket,payload_hash,amz_date);
        string_to_sign = aws_v4_string_to_sign(amz_date, Date::now_iso8601('\0'), region, canonical_request);
        signed_string = Crypto::to_hex(Crypto::hash::hmac_sha256(key, string_to_sign));
        auth_header = aws_v4_auth_header(access_key, Date::now_iso8601('\0'), region, signed_string);

        headers = {
            { "Host", bucket+".s3.amazonaws.com" },
            { "x-amz-content-sha256", payload_hash },
            { "x-amz-date", amz_date },
            { "Authorization", auth_header }
        };
        res = cli.Post(("/"+filename+"?uploadId="+upload_id).c_str(),headers,complete_xml, "text-/xml");

        if ( res.error() != httplib::Error::Success || res->status != 200 ) {
            throw "Failed Multipart upload";
            return res->status;
        }

        return res->status;

    }

    int upload_to_s3(const std::string file_path, const std::string bucket, const std::string region, const std::string access_key, const std::string secret_key) {

        const std::string content_type = "application/x-compressed-tar";

        httplib::Client cli("https://"+bucket+".s3-"+region+".amazonaws.com");
        cli.set_write_timeout(120, 0); // 5 seconds

        auto date = Date::now_rfc5322();
        std::string filename = std::filesystem::path(file_path).filename();

        const std::string signature_string = "PUT\n\n"+content_type+","+content_type+"\n"+date+"\n/"+bucket+"/"+filename;
        const std::string signature_hash = Crypto::base64::encode(Crypto::hash::hmac_sha1(secret_key, signature_string));

        httplib::Headers headers = {
            { "Host", bucket+".s3.amazonaws.com" },
            { "Date", date}, // Ex: Mon, 03 Jan 2022 12:46:34 +0100
            { "Content-Type", content_type},
            { "Authorization", "AWS "+access_key+":"+signature_hash}
        };

        size_t buffer_size = 1<<20; // 10 Megabyte
        char *buffer = new char[buffer_size];
        std::ifstream file(file_path);
        //std::ifstream file(file_path, std::ios::binary);

        // Without content provider
        std::stringstream body;
        body << file.rdbuf();

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[____AWS] Uploading file "+filename+" to "+bucket+" bucket" << std::endl;
        auto res = cli.Put(("/"+filename).c_str(), headers, body.str(), content_type.c_str());
        // TODO: max file to upload is 5GB pero AWS S3, if its bigger send by chunks

        /* With content provider: does not work
        cli.set_default_headers(headers);
        auto res = cli.Put(("/"+filename).c_str(),
          std::filesystem::file_size(file_path),
          [&](size_t offset, size_t length, httplib::DataSink &sink) {
              file.read(buffer, buffer_size);
              size_t count = file.gcount();
              if (!count) {
                  return false;
              }
              //sink.write(buffer+offset, length);
              sink.write(buffer, std::min(length, count*sizeof(char)));
              return true;
          },
          content_type.c_str()
        );
        */


std::cout << res.error() << std::endl;
std::cout << "hola" << std::endl;

        return res->status;

    }

    int download_from_s3(const std::string file_path, const std::string bucket, const std::string region, const std::string access_key, const std::string secret_key) {

        const std::string content_type = "application/x-compressed-tar";

        httplib::Client cli("https://"+bucket+".s3-"+region+".amazonaws.com");

        auto date = Date::now_rfc5322();
        std::string filename = std::filesystem::path(file_path).filename();

        const std::string signature_string = "GET\n\n"+content_type+"\n"+date+"\n/"+bucket+"/"+filename;

        const std::string signature_hash = Crypto::base64::encode(Crypto::hash::hmac_sha1(secret_key, signature_string));

        httplib::Headers headers = {
            { "Host", bucket+".s3.amazonaws.com" },
            { "Date", date}, // Ex: Mon, 03 Jan 2022 12:46:34 +0100
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

