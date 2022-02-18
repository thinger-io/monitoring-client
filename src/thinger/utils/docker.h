#include <filesystem>
#include <vector>
#include <httplib.h>

#include <nlohmann/json.hpp>

// Support from API 1.41
namespace Docker {

    int inspect(const std::string container_id, const std::string dest_path) {

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[_DOCKER] Inspecting container: '" << container_id;
        std::cout << "' and saving result to: '" << dest_path+"/"+container_id+".json" << std::endl;

        httplib::Client cli("unix:/var/run/docker.sock");
        cli.set_default_headers({ { "Host", "localhost" } });

        std::ofstream file(dest_path+"/"+container_id+".json");

        auto res = cli.Get(("/containers/"+container_id+"/json").c_str());//,

        if ( res.error() != httplib::Error::Success ) {
            std::cout << "[_DOCKER] Request error: " << res.error() << std::endl;
            return -1;
        }

        auto res_json = json::parse(res->body);
        file << std::setw(4) << res_json << std::endl;
        if ( ! file ) {
            std::cout << std::fixed << Date::millis()/1000.0 << " ";
            std::cout << "[_DOCKER] There was an error saving result to filesystem" << std::endl;
            return res->status;
        }

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        if (res->status == 200)
            std::cout << "[_DOCKER] Succesfully retrieved information" << std::endl;
        else
            std::cout << "[_DOCKER] Could not retrieve information" << std::endl;

        return res->status;
    }

    int create_from_inspect(const std::string source_path) {

        json inspect_json;

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[_DOCKER] Reading docker inspect json from: '" << source_path << "'" << std::endl;

        std::filesystem::path file(source_path);
        if (std::filesystem::exists(file)) {
            std::ifstream inspect_file(source_path);
            inspect_file >> inspect_json;
        }

        json body = inspect_json["Config"];
        body["HostConfig"] = inspect_json["HostConfig"];
        body["NetworkingConfig"]["EndpointsConfig"] = inspect_json["NetworkSettings"]["Networks"];

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[_DOCKER] Creating container: '" << inspect_json["Name"].get<std::string>() << "'" << std::endl;

        httplib::Client cli("unix:/var/run/docker.sock");
        cli.set_default_headers({ { "Host", "localhost" } });
        cli.set_read_timeout(120, 0); // 120 seconds for commands to execute

        auto res = cli.Post(("/containers/create?name="+inspect_json["Name"].get<std::string>()).c_str(),
          body.dump(), "application/json");

        if ( res.error() == httplib::Error::Read ) {
            std::cout << "[_DOCKER] Error: Timeout waiting for command to execute" << std::endl;
            return -1;
        } else if ( res.error() != httplib::Error::Success ) {
            std::cout << "[_DOCKER] Request error: " << res.error() << std::endl;
            return -1;
        }

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        if (res->status == 201)
            std::cout << "[_DOCKER] Succesfully executed" << std::endl;
        else {
            std::cout << "[_DOCKER] An error occurred while executing" << std::endl;
            std::cout << "[_DOCKER] Error description: " << res->body << std::endl;
        }

        return res->status;
    }

    int exec(const std::string container_id, const std::string command) {

        // POST request: https://docs.docker.com/engine/api/v1.41/#operation/ContainerExec
        // To exec a command in a container, you first need to create an exec instance, then start it.

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[_DOCKER] Executing command: '" << command << "' in container '" << container_id << "'" << std::endl;

        // Create exec instance
        httplib::Client cli("unix:/var/run/docker.sock");
        cli.set_default_headers({ { "Host", "localhost" } });
        cli.set_read_timeout(120, 0); // 120 seconds for commands to execute

        std::stringstream ss(command);

        std::vector<std::string> cmd;
        while(ss.good()) {
            std::string word;
            ss >> word;
            cmd.push_back(word);
        }

        json body1 = {
          {"AttachStdin", false},
          {"AttachStdout", false},
          {"AttachStderr", true}, // will keep the request blocked until it finishes. Not ideal
          {"Tty", false},
          {"Cmd", cmd}
        };

        auto res1 = cli.Post(("/containers/"+container_id+"/exec").c_str(), body1.dump(), "application/json");

        auto res1_json = json::parse(res1->body);

        // Start exec instance
        json body2 = {
          {"Detach", false}, // Does not seem to work!! block until end of command
          {"Tty", false}
        };

        auto res2 = cli.Post(("/exec/"+res1_json["Id"].get<std::string>()+"/start").c_str(), body2.dump(), "application/json");

        if ( res2.error() == httplib::Error::Read ) {
            std::cout << "[_DOCKER] Error: Timeout waiting for command to execute" << std::endl;
            return -1;
        } else if ( res2.error() != httplib::Error::Success ) {
            std::cout << "[_DOCKER] Request error: " << res2.error() << std::endl;
            return -1;
        }

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        if (res2->status == 200)
            std::cout << "[_DOCKER] Succesfully executed" << std::endl;
        else
            std::cout << "[_DOCKER] An error occurred while executing" << std::endl;

        return res2->status;

    }

    int restart(const std::string container_id) {

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[_DOCKER] Restarting container: '" << container_id << "'" << std::endl;

        httplib::Client cli("unix:/var/run/docker.sock");
        cli.set_default_headers({ { "Host", "localhost" } });

        auto res = cli.Post(("/containers/"+container_id+"/restart").c_str(), "t=0", "application/x-www-form-urlencoded");

        if ( res.error() != httplib::Error::Success ) {
            std::cout << "[_DOCKER] Request error: " << res.error() << std::endl;
            return -1;
        }

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        if (res->status == 204)
            std::cout << "[_DOCKER] Succesfully restarted" << std::endl;
        else
            std::cout << "[_DOCKER] Could not be restarted" << std::endl;

        return res->status;
    }

    int start(const std::string container_id) {

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[_DOCKER] Starting container: '" << container_id << "'" << std::endl;

        httplib::Client cli("unix:/var/run/docker.sock");
        cli.set_default_headers({ { "Host", "localhost" } });

        auto res = cli.Post(("/containers/"+container_id+"/start").c_str(), "t=0", "application/x-www-form-urlencoded");

        if ( res.error() != httplib::Error::Success ) {
            std::cout << "[_DOCKER] Request error: " << res.error() << std::endl;
            return -1;
        }

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        if (res->status == 204)
            std::cout << "[_DOCKER] Succesfully restarted" << std::endl;
        else
            std::cout << "[_DOCKER] Could not be restarted" << std::endl;

        return res->status;
    }

    int stop(const std::string container_id) {

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[_DOCKER] Stopping container: '" << container_id << "'" << std::endl;

        httplib::Client cli("unix:/var/run/docker.sock");
        cli.set_default_headers({ { "Host", "localhost" } });

        auto res = cli.Post(("/containers/"+container_id+"/stop").c_str(), "t=0", "application/x-www-form-urlencoded");

        if ( res.error() != httplib::Error::Success ) {
            std::cout << "[_DOCKER] Request error: " << res.error() << std::endl;
            return -1;
        }

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        if (res->status == 204 || res->status == 304)
            std::cout << "[_DOCKER] Succesfully stopped" << std::endl;
        else
            std::cout << "[_DOCKER] Could not be stopped" << std::endl;

        return res->status;
    }

    // It seems that the path has to be relative to the access point
    int copy_from_container(const std::string container_id, const std::string source_path, const std::string dest_path) {

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[_DOCKER] Copying from container: '" << container_id << "' path: '" << source_path;
        std::cout << "' to host path: '" << dest_path << "'" << std::endl;

        // https://docs.docker.com/engine/api/v1.41/#operation/ContainerArchive
        httplib::Client cli("unix:/var/run/docker.sock");
        cli.set_default_headers({ { "Host", "localhost" } });

        std::ofstream file(dest_path); // must have tar extension

        auto res = cli.Get(("/containers/"+container_id+"/archive?path="+source_path).c_str(),
          [&](const char *data, size_t data_length) {
            file.write(data, data_length);
            return true;
          });

        if ( res.error() != httplib::Error::Success ) {
            std::cout << "[_DOCKER] Request error: " << res.error() << std::endl;
            return -1;
        }

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        if (res->status == 200)
            std::cout << "[_DOCKER] Succesfully copied" << std::endl;
        else
            std::cout << "[_DOCKER] Could not be copied" << std::endl;

        return res->status;
    }

    int copy_to_container(const std::string container_id, const std::string source_path, const std::string dest_path) {

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[_DOCKER] Copying from host path: '" << source_path << "' to container '" << container_id;
        std::cout << "' path: '" << dest_path << "'" << std::endl;

        // https://docs.docker.com/engine/api/v1.41/#operation/PutContainerArchive
        httplib::Client cli("unix:/var/run/docker.sock");
        cli.set_write_timeout(120, 0); // 120 seconds
        cli.set_default_headers({ { "Host", "localhost" } });

        size_t buffer_size = 1<<20; // 1 Megabyte
        char *buffer = new char[buffer_size];
        std::ifstream file(source_path);

        /* Whitout content provider
        std::stringstream body;
        body << file.rdbuf();

        auto res = cli.Put(("/containers/"+container_id+"/archive?path="+dest_path).c_str(),
          body.str(), "application/octec-stream");

        */

        unsigned int file_size = std::filesystem::file_size(source_path);
        auto res = cli.Put(("/containers/"+container_id+"/archive?path="+dest_path).c_str(),
          std::filesystem::file_size(source_path),
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
          "application/octet-stream"
        );

        if ( res.error() != httplib::Error::Success ) {
            std::cout << "[_DOCKER] Request error: " << res.error() << std::endl;
            return -1;
        }

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        if (res->status == 200)
            std::cout << "[_DOCKER] Succesfully copied" << std::endl;
        else
            std::cout << "[_DOCKER] Could not be copied" << std::endl;

        return res->status;
    }

};
