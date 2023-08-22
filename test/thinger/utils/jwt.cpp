#include <nlohmann/json.hpp>

#include "../../../src/thinger/utils/jwt.h"

#include <catch2/catch_test_macros.hpp>

//namespace JWT {

  TEST_CASE("JWT", "[token]") {

    std::string token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJqdGkiOiJ0ZXN0Iiwic3ZyIjoidGVzdC50aGluZ2VyLmlvIiwidXNyIjoidGhpbmdlciJ9.iiYN1ZxgseTxvUPAm7TkJRXnJOgLVieE7oZw_3vG9vI";
    std::string token_payload = "eyJqdGkiOiJ0ZXN0Iiwic3ZyIjoidGVzdC50aGluZ2VyLmlvIiwidXNyIjoidGhpbmdlciJ9";
    nlohmann::json payload = {
        {"jti","test"},
        {"svr","test.thinger.io"},
        {"usr","thinger"}
    };

    REQUIRE( JWT::get_payload(token) == payload );

  }

//}
