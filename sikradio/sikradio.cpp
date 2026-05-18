#include "client_config.h"
#include "http_logic.h"
#include "network_logic.h"
#include "url_parser.h"
#include "IStream.h"

#include <iostream>
#include <atomic>
#include <thread>
#include <poll.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>

static void handle_no_metadata(IStream& stream, std::atomic<bool>& is_running, const int verbosity) {
    while (is_running) {
        char buffer[4096];

        ssize_t bytes_read = stream.read(buffer, 4096);

        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // timeout
                if (verbosity > 0) {
                    std::cerr<<"timeout. trying to connect again."<<std::endl;
                }
                break;
            }

            throw std::runtime_error("audio read error");
        }

        if (bytes_read == 0) {
            throw ConnectionClosedException();
        }

        std::cout.write(buffer, bytes_read);
    }
}

static void handle_metadata(IStream& stream, std::atomic<bool>& is_running, const size_t metaint, const int verbosity) {
    char buffer[4096];

    StreamState state = StreamState::AUDIO;
    size_t bytes_to_read = metaint; // starting with the audio data
    std::string metadata_buffer;

    while (is_running) {
        // read only as many bytes as needed for this specific state
        size_t chunk_size = std::min(sizeof(buffer), bytes_to_read);
        ssize_t bytes_read = stream.read(buffer, chunk_size);

        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // tiemout
                if (verbosity > 0) {
                    std::cerr<<"timeout. trying to connect again."<<std::endl;
                }
                break;
            }
            throw std::runtime_error("audio read error");
        }
        if (bytes_read == 0) {
            throw ConnectionClosedException();
        }

        bytes_to_read -= static_cast<size_t>(bytes_read);

        switch (state) {
            case StreamState::AUDIO:
                std::cout.write(buffer, bytes_read);

                if (bytes_to_read == 0) {
                    state = StreamState::MULTIPLIER;
                    bytes_to_read = 1;
                }
                break;

            case StreamState::MULTIPLIER: {
                unsigned char k = static_cast<unsigned char>(buffer[0]);
                size_t metadata_length = k * 16;

                if (metadata_length == 0) {
                    state = StreamState::AUDIO;
                    bytes_to_read = metaint;
                } else {
                    state = StreamState::METADATA;
                    bytes_to_read = metadata_length;
                    metadata_buffer.clear();
                }

                break;
            }

            case StreamState::METADATA:
                metadata_buffer.append(buffer, static_cast<size_t>(bytes_read));

                if (bytes_to_read == 0) {
                    // remove additional '\0' that might have been added
                    std::string clean_meta;
                    for (char c : metadata_buffer) {
                        if (c != '\0') {
                            clean_meta += c;
                        }
                    }

                    if (!clean_meta.empty()) {
                        std::cerr << clean_meta << "\n";
                    }

                    // go back to listening to music
                    state = StreamState::AUDIO;
                    bytes_to_read = metaint;
                }

                break;
        }

    }
}

static void listen_to_music(IStream& stream, std::atomic<bool>& is_running, const size_t metaint, const int verbosity) {
    if (metaint == 0) {
        handle_no_metadata(stream, is_running, verbosity);
        return;
    }

    handle_metadata(stream, is_running, metaint, verbosity);
}

static void initialize_open_ssl() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
}

int main(int argc, char* argv[]) {
    initialize_open_ssl();
    std::atomic<bool> is_running{true};

    std::thread input_thread([&is_running]() {
            struct pollfd pfd;
            pfd.fd = STDIN_FILENO; // listen to the standard input
            pfd.events = POLLIN; // incoming data

            std::string buffer;

            while (is_running) {
                // check if there is a char. if not, stop waiting after 100ms and check the flag
                int ret = poll(&pfd, 1, 100);

                if (ret > 0 && (pfd.revents & POLLIN)) {
                    char c;
                    // read one bit
                    if (read(STDIN_FILENO, &c, 1) > 0) {
                        if (c == '\n') {
                            if (buffer == "quit") {
                                is_running = false; // start quitting
                            }
                            buffer.clear();
                        } else {
                            buffer += c;
                        }
                    }
                }
            }
        });

    int exit_code = EXIT_SUCCESS;

    try {
        ClientConfig config = parse_arguments(argc, argv);

        std::string current_url = config.server_url;
        std::string current_cookie = "";

        std::unique_ptr<IStream> stream;

        while (is_running) {
            auto parsed_opt = parseUrl(current_url);

            if (!parsed_opt.has_value()) {
                throw std::runtime_error("invalid URL format: " + current_url);
            }

            ParsedURL const parsed_url = parsed_opt.value();

            stream = connect_to_server(parsed_url, config);

            send_http_request(*stream, parsed_url, config, current_cookie);

            auto text_opt = server_response_to_text(*stream);
            if (!text_opt.has_value()) {
                //timeout, try again
                continue;
            }

            const std::string server_response_text = text_opt.value();

            auto response_opt = process_http_response(server_response_text);
            if (!response_opt.has_value()) {
                throw std::runtime_error("invalid response");
            }

            const HttpResponseData response_data = response_opt.value();
            if (response_data.critical_error) {
                throw std::runtime_error("critical error " +  std::to_string(response_data.status_code));
            }

            if (response_data.status_code == 200) {
                listen_to_music(*stream, is_running, response_data.icy_metaint, config.verbosity);
            } else if (response_data.status_code == 301 || response_data.status_code == 302) {
                // redirect
                if (response_data.new_location.empty()) {
                    throw std::runtime_error("client redirected to nonexistent location");
                }

                current_url = response_data.new_location;
                if (!response_data.cookie.empty()) {
                    current_cookie = response_data.cookie;
                }

                // TODO: wypisanie odpowiednich logów

                // go back to connecting again
                continue;
            } else {
                // TODO: zmien to
                throw std::runtime_error("unsuported status code " + std::to_string(response_data.status_code));
            }
        }


    } catch (const ConnectionClosedException& e) {
        // server closed the connection
        // TODO: wypisz wszystkie odebrane do tej pory dane
        is_running = false;
        exit_code = EXIT_SUCCESS;
    } catch (const std::exception& e) {
        is_running = false;
        std::cerr << "ERROR: " << e.what() << std::endl;
        exit_code = EXIT_FAILURE;
    }

    is_running = false;
    if (input_thread.joinable()) {
        // wait for the thread to finish
        input_thread.join();
    }

    return exit_code;
}