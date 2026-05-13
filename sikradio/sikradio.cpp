#include "client_config.h"
#include "http_logic.h"
#include "network_logic.h"
#include "url_parser.h"

#include <iostream>

int main(int argc, char* argv[]) {
    try {
        ClientConfig config = parse_arguments(argc, argv);

        // TODO: usuń to potem
        std::cout << "--- WCZYTANA KONFIGURACJA ---\n";
        std::cout << "URL serwera: " << config.server_url << "\n";
        std::cout << "Wymuś IPv4:  " << (config.force_ipv4 ? "TAK" : "NIE") << "\n";
        std::cout << "Wymuś IPv6:  " << (config.force_ipv6 ? "TAK" : "NIE") << "\n";
        std::cout << "Metadane:    " << (config.request_metadata ? "TAK" : "NIE") << "\n";
        std::cout << "Timeout:     " << config.timeout << " ms\n";
        std::cout << "Verbosity:   " << config.verbosity << "\n";
        std::cout << "-----------------------------\n";

        std::string current_url = config.server_url;
        std::string current_cookie = "";

        while (true) {
            auto parsed_opt = parseUrl(current_url);

            if (!parsed_opt.has_value()) {
                throw std::runtime_error("invalid URL format: " + current_url);
            }

            ParsedURL const parsed_url = parsed_opt.value();

            // TODO: wypisywanie w zależności od verbosity

            std::unique_ptr<IStream> stream = connect_to_server(parsed_url, config);

            // TODO: wypisywanie w zależności od verbosity

            send_http_request(*stream, parsed_url, config, current_cookie);

            break; // TODO: zmień tego breaka na warunek wyjścia
        }


    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}