#include <filesystem>
#include <vector>

#include <httplib.h>

#include <nlohmann/json.hpp>

// Support from API 1.41
// TODO add docker port in config, use default also
class Docker {

public:

    static int exec(const std::string container_id, const std::string command) {

        // POST request: https://docs.docker.com/engine/api/v1.41/#operation/ContainerExec
        // To exec a command in a container, you first need to create an exec instance, then start it.

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[_DOCKER] Executing command: '" << command << "' in container '" << container_id << "'" << std::endl;

        // Create exec instance
        httplib::Client cli("unix:/var/run/docker.sock");
        cli.set_default_headers({ { "Host", "localhost" } });

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
//std::cout << res1->body << std::endl;
//std::cout << res1->status << std::endl;

        auto res1_json = json::parse(res1->body);

        // Start exec instance
        json body2 = {
          {"Detach", false}, // Does not seem to work!! block until end of command
          {"Tty", false}
        };

        auto res2 = cli.Post(("/exec/"+res1_json["Id"].get<std::string>()+"/start").c_str(), body2.dump(), "application/json");
//std::cout << res2->body << std::endl;
//std::cout << res2->status << std::endl;

        return res2->status;

    }

    static int restart(const std::string container_id) {

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[_DOCKER] Restarting container: '" << container_id << "'" << std::endl;

        httplib::Client cli("unix:/var/run/docker.sock");
        cli.set_default_headers({ { "Host", "localhost" } });

        auto res = cli.Post(("/containers/"+container_id+"/restart").c_str(), "t=0", "application/x-www-form-urlencoded");
//std::cout << res->body << std::endl;
//std::cout << res->status << std::endl;
        return res->status;
    }

    static int stop(const std::string container_id) {

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[_DOCKER] Stopping container: '" << container_id << "'" << std::endl;

        httplib::Client cli("unix:/var/run/docker.sock");
        cli.set_default_headers({ { "Host", "localhost" } });

        auto res = cli.Post(("/containers/"+container_id+"/stop").c_str(), "t=0", "application/x-www-form-urlencoded");
//std::cout << res->body << std::endl;
//std::cout << res->status << std::endl;
        return res->status;
    }

    // It seems that the path has to be relative to the access point
    static int copy_from_container(const std::string container_id, const std::string source_path, const std::string dest_path) {

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
std::cout << res->body << std::endl;
std::cout << res->status << std::endl;

        return res->status;
    }

    static int copy_to_container(const std::string container_id, const std::string source_path, const std::string dest_path) {

        std::cout << std::fixed << Date::millis()/1000.0 << " ";
        std::cout << "[_DOCKER] Copying from host path: '" << source_path << "' to container '" << container_id;
        std::cout << "' to host path: '" << dest_path << "'" << std::endl;

        // https://docs.docker.com/engine/api/v1.41/#operation/PutContainerArchive
        httplib::Client cli("unix:/var/run/docker.sock");
        cli.set_default_headers({ { "Host", "localhost" } });


        size_t buffer_size = 1<<20; // 1 Megabyte
        char *buffer = new char[buffer_size];
        std::ifstream file(source_path);


        // TODO: content provider
        std::stringstream body;
        body << file.rdbuf();

        auto res = cli.Put(("/containers/"+container_id+"/archive?path="+dest_path).c_str(),
          body.str(), "application/octec-stream");
          //body.str(), "application/x-compressed-tar");

//        std::cout << res->body << std::endl;
//        std::cout << res->status << std::endl;


        // TODO: content provider
        // For some reason is not compiling
        /*unsigned int file_size = std::filesystem::file_size(source_path);
        auto res = cli.Put(("/containers/"+container_id+"/archive?path="+dest_path).c_str(),
          std::filesystem::file_size(source_path),
          [](size_t offset, size_t length, DataSink &sink) {

              file.read(buffer, buffer_size);
              size_t count = file.gcount();
              if (!count) {
                  return false;
              }
              sink.write(buffer+offset, length);
              return true;
          },
          "application/octet-stream"
        );
*/

        // chunks
/*        auto res = cli.Put(("/containers/"+container_id+"/archive?path="+dest_path).c_str,
          [](size_t offset, DataSink &sink) {
              while(fin) {
                  fin.read(buffer, buffer_size);
                  size_t count = fin.gcount();
              }
          },
          "application/octet-stream"
        );
*/

        return res->status;
    }

private:

};
