/*
#include "../../../src/thinger/utils/crypto.h"

#include <catch2/catch_test_macros.hpp>

namespace Crypto {

  TEST_CASE("Crypto", "[base64]") {

    SECTION("Encode") {

      // Valid
      REQUIRE( Crypto::base64::encode("Hello, World!") == "SGVsbG8sIFdvcmxkIQ==" );
      REQUIRE( Crypto::base64::encode("Testing 123") == "VGVzdGluZyAxMjM=" );
      REQUIRE( Crypto::base64::encode("").empty() );

      // Invalid
      REQUIRE( Crypto::base64::encode("Invalid@String") == "This contains characters not legal in a base64 encoded string." );

    }

    SECTION("Decode") {

      // Valid
      REQUIRE( Crypto::base64::decode("SGVsbG8gd29ybGQhIQ==") == "Hello world!!!" );

      // Invalid
      REQUIRE( Crypto::base64::decode("NotBase64Encoded") == "This contains characters not legal in a base64 encoded string." );
      REQUIRE( Crypto::base64::decode("ASNFZ4mrze8") == "This contains characters not legal in a base64 encoded string." );
    }
  }

}

 */