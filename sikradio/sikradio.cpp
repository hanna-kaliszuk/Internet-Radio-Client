#include "client_config.h"
#include "http_logic.h"
#include "network_logic.h"
#include "url_parser.h"

#include <iostream>
#include <thread>

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

        std::unique_ptr<IStream> stream;

        std::thread input_thread([]() {
            std::string line;
            // read from the terminal
            while (std::getline(std::cin, line)) {
                if (line == "quit") {
                    std::exit(0);
                }
            }
        });

        input_thread.detach();

        while (true) {
            auto parsed_opt = parseUrl(current_url);

            if (!parsed_opt.has_value()) {
                throw std::runtime_error("invalid URL format: " + current_url);
            }

            ParsedURL const parsed_url = parsed_opt.value();

            // TODO: wypisywanie w zależności od verbosity

            stream = connect_to_server(parsed_url, config);

            // TODO: wypisywanie w zależności od verbosity

            send_http_request(*stream, parsed_url, config, current_cookie);

            auto text_opt = server_response_to_text(*stream);
            if (!text_opt.has_value()) {
                //timeout, try again
                continue;
            }

            const std::string server_response_text = text_opt.value();

            auto response_opt = process_http_response(*text_opt);
            if (!response_opt.has_value()) {
                throw std::runtime_error("invalid response");
            }

            const HttpResponseData response_data = response_opt.value();
            if (response_data.critical_error) {
                throw std::runtime_error("critical error");
            }

            if (response_data.status_code == 200) {
                char buffer[4096];

                while (true) {
                    ssize_t bytes_read = stream->read(buffer, 4096);

                    if (bytes_read < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // timeout
                            // TODO: moze wypisac ze timeout
                            break;
                        }

                        throw std::runtime_error("audio read error");
                    }

                    if (bytes_read == 0) {
                        throw ConnectionClosedException();
                    }

                    std::cout.write(buffer, bytes_read);

                }
            } else {
                // TODO: zmien to
                throw std::runtime_error("temporary error. FIX IT");
            }
        }


    } catch (const ConnectionClosedException& e) {
        // server closed the connection
        // TODO: wypisz wszystkie odebrane do tej pory dane
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}