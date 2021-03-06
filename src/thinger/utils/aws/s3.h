
#include <httplib.h>

#include "../date.h"
#include "../crypto.h"
#include "../http_status.h"

class S3 {

public:

    class AWSV4 {
    // Handles the construction of the signature AWS Signature Version 4 (SigV4)

        public:

        AWSV4(std::string& access_key, std::string& secret_key, std::string& region, const std::string& service) :
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

        std::string generate_key(Date date) const {

            const std::string kDate = Crypto::hash::hmac_sha256("AWS4"+secret_key_, date.to_iso8601('\0',false,"utc"));
            const std::string kRegion = Crypto::hash::hmac_sha256(kDate, region_);
            const std::string kService = Crypto::hash::hmac_sha256(kRegion, service_);
            const std::string kSigning = Crypto::hash::hmac_sha256(kService, "aws4_request");

            return kSigning;
        }

        std::string generate_string_to_sign(Date& date, std::string& canonical_request) const {

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

        MultipartUpload(std::string& bucket, std::string& region, std::string& access_key, std::string& secret_key, const std::string& file_path) :
            bucket_(bucket),
            region_(region),
            access_key_(access_key),
            secret_key_(secret_key),
            file_path_(file_path)
        {
            url = bucket+".s3-"+region+".amazonaws.com";
            awsv4 = std::make_unique<AWSV4>(access_key, secret_key, region, "s3");

            // Initialize http client
            cli = std::make_unique<httplib::Client>("https://"+url);
            httplib::Headers headers = {
                { "Host", bucket_+".s3-"+region_+".amazonaws.com" },
            };
            cli->set_default_headers(headers);
            cli->set_write_timeout(30, 0); // 30 seconds
            cli->set_read_timeout(30, 0); // 30 seconds
            cli->set_keep_alive(true);

            // Set class attributes
            // open file and calculate number of parts
            std::ifstream file(file_path_);
            auto file_size = std::filesystem::file_size(file_path_);
            parts = file_size / buffer_size;
            if (file_size % buffer_size != 0) parts = parts+1;

            // TODO: request list of parts to S3 and form XML body to complete from the response
            parts_list = std::unique_ptr<MPUPart[]>{new MPUPart[parts+1]};

            filename = std::filesystem::path(file_path_).filename();

        }

        /**
            Handles the full lifecycle of the multipart upload. Initiate Upload, Upload Part, Complete Upload, Abort Upload, List Parts.
            @return Boolean indicating if the upload operation was successful
        */
        bool upload() {

            if (!initiate_upload()) {
                return false;
            }

            // Upload parts
            if (!upload_parts()) {
                abort_upload();
                return false;
            }

            if (!complete_upload()) {
                abort_upload();
                return false;
            }

            return true;

        }

        ~MultipartUpload() = default;

        private:
        std::string bucket_, region_, access_key_, secret_key_, file_path_, filename;
        unsigned int parts;

        const std::string content_type = "application/x-compressed-tar";
        const size_t buffer_size = 10<<20; // 10 Megabytes -> with a max of 10.000 parts allows a file of up to 100GiB

        std::string upload_id = "";

        std::string url;
        std::unique_ptr<AWSV4> awsv4{};
        std::unique_ptr<httplib::Client> cli{};

        struct MPUPart {
            unsigned int part_id;
            std::string ETag;
        };
        std::unique_ptr<MPUPart[]> parts_list; // TODO: request list of parts to S3 and form XML body to complete from the response

        bool initiate_upload() {

            std::cout << std::fixed << Date::millis()/1000.0 << " ";
            std::cout << "[____AWS] Inititiating upload of "+file_path_ << std::endl;

            std::string payload = "";
            auto res = request("POST", "/"+filename,"uploads=",payload, content_type);

            if ( res.error() != httplib::Error::Success || !HttpStatus::isSuccessful(res->status) ) {
                std::cerr << std::fixed << Date::millis()/1000.0 << " ";
                std::cerr << "[____AWS] Failed initiating upload "+filename << std::endl;
                return HttpStatus::isSuccessful(res->status);
            }

            upload_id = XML::get_element_value(res->body, "UploadId");

            std::cout << std::fixed << Date::millis()/1000.0 << " ";
            std::cout << "[____AWS] Upload initiated with upload id "+upload_id << std::endl;
            return HttpStatus::isSuccessful(res->status);
        }

        bool abort_upload() {

            std::cout << std::fixed << Date::millis()/1000.0 << " ";
            std::cout << "[____AWS] Aborting multipart upload for file: " << file_path_ << "; and upload id: " << upload_id << std::endl;

            std::string payload = "";
            auto res = request("DELETE", "/"+filename,"uploadId="+upload_id,payload,"text/plain");

            if ( res.error() != httplib::Error::Success || !HttpStatus::isSuccessful(res->status) ) {
                std::cerr << std::fixed << Date::millis()/1000.0 << " ";
                std::cerr << "[____AWS] Failed aborting upload id"+upload_id << std::endl;
            }

            return HttpStatus::isSuccessful(res->status);
        }

        bool upload_parts() {
            std::string payload_hash, canonical_request, auth_header;
            httplib::Headers headers;
            Date date;

            auto *buffer = new char[buffer_size];

            // Open and calculate part size
            std::ifstream file(file_path_);
            auto file_size = std::filesystem::file_size(file_path_);
            auto last_part_size = file_size % buffer_size;

            for (unsigned int i = 1; i < parts; i++) { // send part by part

                file.read(buffer, buffer_size);

                date = Date();

                std::cout << std::fixed << Date::millis()/1000.0 << " ";
                std::cout << "[____AWS] Multipart upload of "+filename+"; Uploading part "+std::to_string(i)+" out of "+std::to_string(parts) << std::endl;

                auto res = request("PUT","/"+filename,
                    "partNumber="+std::to_string(i)+"&uploadId="+upload_id,buffer,buffer_size,content_type);

                if ( res.error() != httplib::Error::Success || !HttpStatus::isSuccessful(res->status) ) {
                    std::cerr << std::fixed << Date::millis()/1000.0 << " ";
                    std::cerr << "[____AWS] Failed Multipart upload "+filename+"; Part "+std::to_string(i)+" out of "+std::to_string(parts) << std::endl;
                    return HttpStatus::isSuccessful(res->status);
                }

                parts_list[i-1] = MPUPart{i, res->get_header_value("ETag")};

            }

            // Upload last part
            std::cout << std::fixed << Date::millis()/1000.0 << " ";
            std::cout << "[____AWS] Multipart upload of "+filename+"; Uploading part "+std::to_string(parts)+" out of "+std::to_string(parts) << std::endl;

            file.read(buffer, buffer_size);

            auto res = request("PUT","/"+filename,
                "partNumber="+std::to_string(parts)+"&uploadId="+upload_id,buffer,last_part_size,content_type);

            if ( res.error() != httplib::Error::Success || !HttpStatus::isSuccessful(res->status) ) {
                std::cerr << std::fixed << Date::millis()/1000.0 << " ";
                std::cerr << "[____AWS] Failed Multipart upload "+filename+"; Part "+std::to_string(parts)+" out of "+std::to_string(parts) << std::endl;
                return HttpStatus::isSuccessful(res->status);
            }

            parts_list[parts-1] = MPUPart{parts, res->get_header_value("ETag")};
            return HttpStatus::isSuccessful(res->status);
        }

        bool complete_upload() {
            // TODO: complete_upload: request list of parts to S3 and form last request with this response

            std::string complete_xml = generate_xml_complete_mpu();

            std::cout << std::fixed << Date::millis()/1000.0 << " ";
            std::cout << "[____AWS] Finishing upload of "+file_path_ << std::endl;

            return request("POST","/"+filename,"uploadId="+upload_id,complete_xml, "text/xml");
        }

        httplib::Result request(
          const std::string& method,
          const std::string& path,
          const std::string& query_parameters,
          std::string& payload,
          const std::string& c_type)
        {
            return request(method,path,query_parameters,payload.c_str(),payload.size(),c_type);
        }

        httplib::Result request(
          const std::string& method,
          const std::string& path,
          const std::string& query_parameters,
          const char *buffer,
          const size_t buffer_size,
          const std::string& c_type)
        {
            std::string payload_hash, canonical_request, auth_header;
            httplib::Headers headers;
            auto date = Date();

            payload_hash = Crypto::hash::sha256(buffer,buffer_size);

            canonical_request = generate_canonical_request(method,path,query_parameters,payload_hash,date);
            auth_header = awsv4->get_auth_header(date, canonical_request);

            headers = {
                { "x-amz-content-sha256", payload_hash },
                { "x-amz-date", date.to_iso8601('\0',true,"utc") },
                { "Authorization", auth_header }
            };

            if (method == "POST") {
                return cli->Post((path+"?"+query_parameters).c_str(),headers,buffer,buffer_size,c_type.c_str());
            } else if (method == "PUT") {
                return cli->Put((path+"?"+query_parameters).c_str(),headers,buffer,buffer_size,c_type.c_str());
            }

            return httplib::Result{nullptr, httplib::Error::Unknown};;

        }

        std::string generate_canonical_request(
          const std::string& method,
          const std::string& path,
          const std::string& query_parameters,
          std::string& payload_hash,
          Date date) const
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

        std::string generate_xml_complete_mpu() const {

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
