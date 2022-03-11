
#include <httplib.h>

#include "../date.h"
#include "../crypto.h"

class S3 {

public:

    class AWSV4 {

        public:

        AWSV4(std::string& access_key, std::string& secret_key, std::string& region, const std::string service) :
            access_key_(access_key),
            secret_key_(secret_key),
            region_(region),
            service_(service)
        {}

        std::string get_auth_header(Date date, std::string& canonical_request) {

            std::string signed_string = sign_string(date, canonical_request);

            const std::string auth_header = std::string("AWS4-HMAC-SHA256 ")+
                "Credential="+access_key_+"/"+date.to_iso8601('\0',false,"utc")+"/"+region_+"/"+service_+"/aws4_request,"+
                "SignedHeaders=host;x-amz-content-sha256;x-amz-date,"+
                "Signature="+signed_string;

            return auth_header;
        }


        private:

        std::string access_key_, secret_key_, region_, service_;

        std::string generate_key(Date date) {

            const std::string kDate = Crypto::hash::hmac_sha256("AWS4"+secret_key_, date.to_iso8601('\0',false,"utc"));
            const std::string kRegion = Crypto::hash::hmac_sha256(kDate, region_);
            const std::string kService = Crypto::hash::hmac_sha256(kRegion, service_);
            const std::string kSigning = Crypto::hash::hmac_sha256(kService, "aws4_request");

            return kSigning;
        }

        std::string generate_string_to_sign(Date& date, std::string& canonical_request) {

            const std::string string_to_sign =

                std::string("AWS4-HMAC-SHA256\n")+
                date.to_iso8601('\0',true,"utc")+"\n"+
                date.to_iso8601('\0',false,"utc")+"/"+region_+"/s3/aws4_request\n"+
                Crypto::hash::sha256(canonical_request);

            return string_to_sign;
        }

        std::string sign_string(Date date, std::string& canonical_request) {

            std::string key = generate_key(date);
            std::string string_to_sign = generate_string_to_sign(date, canonical_request);

            return Crypto::to_hex(Crypto::hash::hmac_sha256(key, string_to_sign));

        }

    };

    class MultipartUpload {

        public:

        MultipartUpload(std::string& bucket, std::string& region, std::string& access_key, std::string& secret_key) :
            bucket_(bucket),
            region_(region),
            access_key_(access_key),
            secret_key_(secret_key)
        {
            url = bucket+".s3-"+region+".amazonaws.com";
            awsv4 = new AWSV4(access_key, secret_key, region, "s3");

            // Initialize http client
            cli = new httplib::Client("https://"+url);
            httplib::Headers headers = {
                { "Host", bucket_+".s3-"+region_+".amazonaws.com" },
            };
            cli->set_default_headers(headers);
            cli->set_write_timeout(30, 0); // 30 seconds
            cli->set_read_timeout(30, 0); // 30 seconds
            cli->set_keep_alive(true);
        }

        struct MPUPart {
            unsigned int part_id;
            std::string ETag;
        };

        std::string initiate_upload(const std::string& file_path) {

            Date date = Date();
            std::string filename = std::filesystem::path(file_path).filename();

            // Form aws v4 signature
            std::string payload_hash = Crypto::hash::sha256("");
            std::string canonical_request = generate_canonical_request("POST", "/"+filename,"uploads=",bucket_, payload_hash, date);

            std::string auth_header = awsv4->get_auth_header(date, canonical_request);

            httplib::Headers headers = {
                { "x-amz-content-sha256", payload_hash },
                { "x-amz-date", date.to_iso8601('\0',true,"utc") },
                { "Authorization", auth_header }
            };

            auto res = cli->Post(("/"+filename+"?uploads").c_str(),headers,"",content_type.c_str());

            if ( res.error() != httplib::Error::Success || res->status != 200 ) {
                std::cerr << std::fixed << Date::millis()/1000.0 << " ";
                std::cerr << "[____AWS] Failed initiating upload "+filename << std::endl;
                return std::to_string(res->status);
            }

            std::string upload_id = XML::get_element_value(res->body, "UploadId");
            return upload_id;
        }

        int upload(std::string& upload_id, const std::string& file_path) {

            std::string payload_hash, canonical_request, auth_header;
            httplib::Headers headers;
            Date date;
            std::string filename = std::filesystem::path(file_path).filename();

            size_t buffer_size = 10<<20; // 10 Megabytes -> with a max of 10.000 parts allows a file of up to 100GiB
            char *buffer = new char[buffer_size];

            // open file and calculate number of parts
            std::ifstream file(file_path);
            auto file_size = std::filesystem::file_size(file_path);
            unsigned int parts = file_size / buffer_size;
            auto last_part_size = file_size % buffer_size;
            MPUPart parts_list[parts+1]; // TODO: request list of parts to S3 and form XML body to complete from the response
            if (last_part_size != 0) parts = parts+1;

            for (unsigned int i = 1; i < parts; i++) { // send part by part

                file.read(buffer, buffer_size);

                date = Date();

                std::cout << std::fixed << Date::millis()/1000.0 << " ";
                std::cout << "[____AWS] Multipart upload of "+filename+"; Uploading part "+std::to_string(i)+" out of "+std::to_string(parts) << std::endl;

                payload_hash = Crypto::hash::sha256(buffer, buffer_size);
                canonical_request = generate_canonical_request("PUT","/"+filename,
                    "partNumber="+std::to_string(i)+"&uploadId="+upload_id,bucket_,payload_hash,date);

                auth_header = awsv4->get_auth_header(date, canonical_request);

                headers = {
                    { "x-amz-content-sha256", payload_hash },
                    { "x-amz-date", date.to_iso8601('\0',true,"utc") },
                    { "Authorization", auth_header }
                };

                auto res = cli->Put(("/"+filename+"?partNumber="+std::to_string(i)+"&uploadId="+upload_id).c_str(),headers,buffer,buffer_size,content_type.c_str());

                if ( res.error() != httplib::Error::Success || res->status != 200 ) {
                    std::cerr << std::fixed << Date::millis()/1000.0 << " ";
                    std::cerr << "[____AWS] Failed Multipart upload "+filename+"; Part "+std::to_string(i)+" out of "+std::to_string(parts) << std::endl;
                    return res->status;
                }

                parts_list[i-1] = MPUPart{i, res->get_header_value("ETag")};

            }

            std::cout << std::fixed << Date::millis()/1000.0 << " ";
            std::cout << "[____AWS] Multipart upload of "+filename+"; Uploading part "+std::to_string(parts)+" out of "+std::to_string(parts) << std::endl;

            file.read(buffer, buffer_size);
            date = Date();
            payload_hash = Crypto::hash::sha256(buffer, last_part_size);
            canonical_request = generate_canonical_request("PUT","/"+filename,
                "partNumber="+std::to_string(parts)+"&uploadId="+upload_id,bucket_,payload_hash,date);

            auth_header = awsv4->get_auth_header(date, canonical_request);

            headers = {
                { "x-amz-content-sha256", payload_hash },
                { "x-amz-date", date.to_iso8601('\0',true,"utc") },
                { "Authorization", auth_header }
            };

            auto res = cli->Put(("/"+filename+"?partNumber="+std::to_string(parts)+"&uploadId="+upload_id).c_str(),headers,buffer,last_part_size,content_type.c_str());

            if ( res.error() != httplib::Error::Success || res->status != 200 ) {
                std::cerr << std::fixed << Date::millis()/1000.0 << " ";
                std::cerr << "[____AWS] Failed Multipart upload "+filename+"; Part "+std::to_string(parts)+" out of "+std::to_string(parts) << std::endl;
                return res->status;
            }

            parts_list[parts-1] = MPUPart{parts, res->get_header_value("ETag")};

            // Complete upload
            date = Date();
            std::string complete_xml = generate_xml_complete_mpu(parts_list, parts);
            payload_hash = Crypto::hash::sha256(complete_xml);

            canonical_request = generate_canonical_request("POST","/"+filename,"uploadId="+upload_id,bucket_,payload_hash,date);
            auth_header = awsv4->get_auth_header(date, canonical_request);

            headers = {
                { "x-amz-content-sha256", payload_hash },
                { "x-amz-date", date.to_iso8601('\0',true,"utc") },
                { "Authorization", auth_header }
            };
            res = cli->Post(("/"+filename+"?uploadId="+upload_id).c_str(),headers,complete_xml, "text-/xml");

            if ( res.error() != httplib::Error::Success || res->status != 200 ) {
                std::cerr << std::fixed << Date::millis()/1000.0 << " ";
                std::cerr << "[____AWS] Failed Multipart upload "+filename+" on complete upload request" << std::endl;
                return res->status;
            }

            return res->status;

        }

        // TODO: abort_upload
        // TODO: complete_upload: request list of parts to S3 and form last request with this response

        ~MultipartUpload()
        {
            delete awsv4;
            delete cli;
        }

        protected:
        std::string bucket_, region_, access_key_, secret_key_;

        private:
        const std::string content_type = "application/x-compressed-tar";

        std::string url;
        AWSV4 *awsv4;
        httplib::Client *cli;

        std::string generate_canonical_request(
          const std::string method,
          const std::string path,
          const std::string query_parameters,
          std::string& bucket,
          std::string& payload_hash,
          Date date)
        {

            const std::string canonical_request =
                method+"\n"+
                path+"\n"+
                query_parameters+"\n"+
                "host:"+url+"\n"+
                "x-amz-content-sha256:"+payload_hash+"\n"+
                "x-amz-date:"+date.to_iso8601('\0',true,"utc")+"\n\n"+
                "host;x-amz-content-sha256;x-amz-date\n"+
                payload_hash;

            return canonical_request;
        }

        std::string generate_xml_complete_mpu(MPUPart *parts_list, unsigned int parts) {

            std::string xml_res = "<CompleteMultipartUpload>\n";
            for (int i=0; i < parts; i++) {
                xml_res += "<Part>\n<PartNumber>"+std::to_string(parts_list[i].part_id)+"</PartNumber>\n";
                xml_res += "<ETag>"+parts_list[i].ETag+"</ETag>\n</Part>";
            }
            xml_res += "\n</CompleteMultipartUpload>";

            return xml_res;
        }

    };

};
