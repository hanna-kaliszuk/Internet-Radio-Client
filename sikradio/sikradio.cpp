#include "client_config.h"

#include <iostream>

int main(int argc, char* argv[]) {
    try {
        ClientConfig config = parse_arguments(argc, argv);

        std::cout << "--- WCZYTANA KONFIGURACJA ---\n";
        std::cout << "URL serwera: " << config.server_url << "\n";
        std::cout << "Wymuś IPv4:  " << (config.force_ip4 ? "TAK" : "NIE") << "\n";
        std::cout << "Wymuś IPv6:  " << (config.force_ip6 ? "TAK" : "NIE") << "\n";
        std::cout << "Metadane:    " << (config.request_metadata ? "TAK" : "NIE") << "\n";
        std::cout << "Timeout:     " << config.timeout << " ms\n";
        std::cout << "Verbosity:   " << config.verbosity << "\n";
        std::cout << "-----------------------------\n";
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}