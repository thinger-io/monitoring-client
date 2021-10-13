# Try to find jsoncpp library
# This will define:
#
#  JsonCpp_FOUND        - jsoncpp library is available
#  JsonCpp_INCLUDE_DIR  - Where the json.h header file is
#  JsonCpp_LIBRARIES    - The libraries to link in.
#

FIND_PATH(JsonCpp_INCLUDE_DIR
  NAMES
    json/json.h
  PATHS
    /usr/local/include
    /usr/local/include/jsoncpp
    /usr/include
    /usr/include/jsoncpp
)

find_library(JsonCpp_LIBRARIES
  NAMES
    jsoncpp
    libjsoncpp
  PATHS
    /usr/lib
    /usr/local/lib
)

if(JsonCpp_LIBRARIES AND JsonCpp_INCLUDE_DIR)
  set(JsonCpp_FOUND 1)
  mark_as_advanced(JsonCpp_LIBRARIES JsonCpp_INCLUDE_DIR)
else()
  set(JsonCpp_FOUND 0)
endif()

