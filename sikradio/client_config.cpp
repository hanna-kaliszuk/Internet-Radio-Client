#include "client_config.h"

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unistd.h>


// helper fo print usage instructions.
static void print_usage(const std::string_view prog_name) {
    std::cerr << "usage: " << prog_name << " -u <URL> [-4] [-6] [-t <timeout>] [-m] [-v <level>] [-q]\n";
}

ClientConfig parse_arguments(int argc, char* argv[]) {
    ClientConfig config; 
    int opt; 

    // to track duplicates 
    bool seen_u = false, seen_4 = false, seen_6 = false;
    bool seen_t = false, seen_m = false, seen_v = false, seen_q = false;

    while ((opt = getopt(argc, argv, "u:46t:mv:q")) != -1) {
        switch (opt) {
            case 'u':
                if (seen_u) throw std::invalid_argument("parameter '-u' provided multiple times.");

                seen_u = true; 
                config.server_url = optarg; 
                break;

            case 'm':
                if (seen_m) throw std::invalid_argument("parameter '-m' provided multiple times");

                seen_m = true;
                config.request_metadata = true;
                break;

            case 't':
                if (seen_t) throw std::invalid_argument("parameter '-t' provided multiple times");

                seen_t = true;
                try {
                    long long t = std::stoll(optarg);

                    // range validation 
                    if (t < 100 || t > 100000) {
                        throw std::out_of_range("value for -t must be between 100 and 100000");
                    }
                    config.timeout = static_cast<uint32_t>(t);
                } catch (const std::exception&) {
                    throw std::invalid_argument("invalid numeric argument for -t");
                }
                break;

            case '4':
                if (seen_4) throw std::invalid_argument("parameter '-4' provided multiple times");

                seen_4 = true; 
                config.force_ipv4 = true;
                break;

            case '6':
                if (seen_6) throw std::invalid_argument("parameter '-6' provided multiple times");

                seen_6 = true; 
                config.force_ipv6 = true;
                break;

            case 'v':
                if (seen_v) throw std::invalid_argument("parameter '-v' provided multiple times");

                seen_v = true;
                try {
                    int v = std::stoi(optarg);

                    // range validation
                    if (v < 0 || v > 4) {
                        throw std::out_of_range("value for -v must be between 0 and 4");
                    }

                    config.verbosity = v;
                } catch (const std::exception&) {
                    throw std::invalid_argument("invalid numeric argument for -v");
                }
                break;

            case 'q':
                if (seen_q)throw std::invalid_argument("parameter '-q' provided multiple times");

                seen_q = true;
                config.verbosity = 0;
                break;
            
            case '?':
            default:
                print_usage(argv[0]);
                throw std::invalid_argument("unknown parameter provided");
        }
    }

    // post parse valildation 
    if (!seen_u) {
        print_usage(argv[0]);
        throw std::invalid_argument("missing required parameter '-u' (server URL)");
    }

    if (seen_v && seen_q) {
        print_usage(argv[0]);
        throw std::invalid_argument("options '-v' and '-q' are mutually exclusive");
    }

    // if both -4 and -6 are provided (or neither), let getaddrinfo decide
    if (config.force_ipv4 && config.force_ipv6) {
        config.force_ipv4 = false;
        config.force_ipv6 = false;
    }

    return config;
}