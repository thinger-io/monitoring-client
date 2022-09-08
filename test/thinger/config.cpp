#include "../../src/thinger/config.h"

#include <catch2/catch_test_macros.hpp>

#include <regex>

namespace thinger::monitor {

    TEST_CASE("config", "[config]") {

        /*
        json jempty = {};

        json jconfig = {{"server", {
                          {"url",   "test.aws.thinger.io"},
                          {"user",  "thinger"},
                          {"ssl",   false}}},
                        {"device", {
                          {"id",          "test"},
                          {"name",        "test name"},
                          {"credentials", "6OO$Cj#VH_S_6slz"}}},
                        {"resources", {
                          {"defaults",    true},
                          {"drives",      {"nvme0n1"}},
                          {"filesystems", {"/"}},
                          {"interfaces",  {"wlan0"}}}},
                        {"storage", {
                          {"S3", {
                            {"bucket",     "monitor_s3"},
                            {"region",     "region_s3"},
                            {"access_key", "access_key_s3"},
                            {"secret_key", "secret_key_s3"}}},
                          {"GC", {
                            {"bucket",     "monitor_gc"},
                            {"region",     "region_gc"},
                            {"access_key", "access_key_gc"},
                            {"secret_key", "secret_key_gc"}}}}},
                        {"backups", {
                          {"compose_path",    "/"},
                          {"data_path",       "/data"},
                          {"endpoints_token", "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJqdGkiOiJtb25pdG9yX3Rlc3QiLCJzdnIiOiIxOTIuMTY4LjEuMSIsInVzciI6InRoaW5nZXIifQ.by4zF0i8tfMz8MsQOxDoy-q1IY3yeChKGSxHlQucKD0"},
                          {"storage",         "S3"},
                          {"system",          "platform"}}}};
        */

        Config empty("../test/thinger/empty.json");
        Config config("../test/thinger/config.json");

        SECTION("Getters") {

              REQUIRE( empty.get_url().empty() );
              REQUIRE( config.get_url() == "test.aws.thinger.io" );

              REQUIRE( empty.get_user().empty() );
              REQUIRE( config.get_user() == "thinger" );

              REQUIRE( empty.get_ssl() == true );
              REQUIRE( config.get_ssl() == false );

              REQUIRE( empty.get_url().empty() );
              REQUIRE( config.get_url() == "test.aws.thinger.io" );

              REQUIRE( empty.get_id().empty() );
              REQUIRE( config.get_id() == "test" );

              REQUIRE( empty.get_name().empty() );
              REQUIRE( config.get_name() == "test name" );

              REQUIRE( empty.get_credentials().empty() );
              REQUIRE( config.get_credentials() == "6OO$Cj#VH_S_6slz" );

              REQUIRE( config.get_storage()                        == "S3" );
              REQUIRE( config.get_bucket(config.get_storage())     == "monitor_s3" );
              REQUIRE( config.get_region(config.get_storage())     == "region_s3" );
              REQUIRE( config.get_access_key(config.get_storage()) == "access_key_s3" );
              REQUIRE( config.get_secret_key(config.get_storage()) == "secret_key_s3" );

              REQUIRE( config.get_compose_path() == "/compose/test" );
              REQUIRE( config.get_data_path() == "/data/test" );

              REQUIRE( config.get_defaults() == true );
              REQUIRE( empty.get_defaults()  == false );

        }

        SECTION("Setters") {

              const std::string url = "test1.aws.thinger.io";
              empty.set_url(url);
              config.set_url(url);
              REQUIRE( empty.get_url() == url );
              REQUIRE( config.get_url() == url );

              const std::string user = "test1";
              empty.set_user(user);
              config.set_user(user);
              REQUIRE( empty.get_user() == user );
              REQUIRE( config.get_user() == user );

              const bool ssl = false;
              empty.set_ssl(ssl);
              config.set_ssl(ssl);
              REQUIRE( empty.get_ssl() == ssl );
              REQUIRE( config.get_ssl() == ssl );

              // TODO: set device (is machine dependant)

        }

        SECTION("pson") {



        }

    }
}

namespace thinger::monitor::config {

    TEST_CASE("Template get", "[config]") {

        json j = {{"string", "string"},
                  {"int", 5},
                  {"double", 2.5},
                  {"bool", true},
                  {"object", {
                    {"string", "string"},
                    {"array", {"one", "two", "three"}}}}};

        REQUIRE( get(j, "/string"_json_pointer, std::string("")) == "string" );
        REQUIRE( get(j, "/string/test"_json_pointer, std::string("")).empty() );

        REQUIRE( get(j, "/int"_json_pointer, 0) == 5 );
        REQUIRE( get(j, "/int/test"_json_pointer, 0) == 0 );

        REQUIRE( get(j, "/double"_json_pointer, 0.0) == 2.5 );
        REQUIRE( get(j, "/double/test"_json_pointer, 0.0) == 0 );

        REQUIRE( get(j, "/bool"_json_pointer, false) == true );
        REQUIRE( get(j, "/bool/test"_json_pointer, false) == false );

        REQUIRE( get(j, "/object"_json_pointer, json({})) == j["object"] );
        REQUIRE( get(j, "/object/test"_json_pointer, json({})) == json({}) );
    }

}

namespace thinger::monitor::utils {

    TEST_CASE("Generate credentials", "[config]") {

        const std::string credentials16 = generate_credentials(16);
        const std::string credentials32 = generate_credentials(32);

        REQUIRE( credentials16.size() == 16 );
        REQUIRE( credentials32.size() == 32 );
    }

    TEST_CASE("pson/json conversion", "[config]") {

        pson pempty;
        json jempty({});

        pson p;
        p["string"] = "string";
        p["int"] = 2;
        p["float"] = 5.2f;
        p["bool"] = true;
        pson_object const& obj = p["empty_obj"];
        pson_array const& arr = p["empty_arr"];

        pson_array& p_arr =  p["array"];
        p_arr.add("one");
        p_arr.add("two");
        p_arr.add("three");

        pson_object& p_obj = p["object"];
        p_obj["string"] = "string";

        json j = {{"string", "string"},
                  {"int", 2},
                  {"float", 5.2f},
                  {"bool", true},
                  {"array", {"one", "two", "three"}},
                  {"object", {
                    {"string", "string"}}},
                  {"empty_obj", json({})},
                  {"empty_arr", json::array()}};
                     // TODO: numbers, bools, arrays inside object not implemented

        REQUIRE( to_json(p) == j );

        REQUIRE( to_json(to_pson(j)) == j );

        REQUIRE( to_json(pempty) == jempty );

    }
}
