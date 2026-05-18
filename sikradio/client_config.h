#pragma once 

#include <string>
#include <cstdint>

/**
* @brief configuration of the client
**/
struct ClientConfig {
    std::string server_url;    // -u
    bool request_metadata = false;  // -m
    uint32_t timeout = 5000;        // -t
    // if both -4 and -6 are provided (or neither) both forces will be false
    bool force_ipv4 = false;         // -4
    bool force_ipv6 = false;         // -6
    int verbosity = 2;              // -v
};

ClientConfig parse_arguments(int argc, char* argv[]);