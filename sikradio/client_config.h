#pragma once 

#include <string>
#include <cstdint>

/**
* @brief configuration of the client
**/
struct ClientConfig {
    std::string server_url = "";    // -u
    bool request_metadata = false;  // -m
    uint32_t timeout = 5000;        // -t
    bool force_ip4 = false;         // -4
    bool force_ip6 = false;         // -6
    int verbosity = 2;              // -v
};

ClientConfig parse_arguments(int argc, char* argv[]);