#include "client_config.h"
#include "http_logic.h"
#include "network_logic.h"
#include "url_parser.h"
#include "IStream.h"
#include "logger.h"

#include <iostream>
#include <atomic>
#include <thread>
#include <poll.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>

static std::string stream_result_to_string(StreamResult result) {
    switch (result) {
        case StreamResult::OK:
            return "OK";
        case StreamResult::TIMEOUT:
            return "TIMEOUT";
        case StreamResult::CLOSED_BY_SERVER:
            return "CLOSED_BY_SERVER";
        case StreamResult::STOPPED_BY_CLIENT:
            return "STOPPED_BY_CLIENT";
    }

    return "UNKNOWN";
}

static StreamResult handle_no_metadata(IStream& stream, std::atomic<bool>& is_running, const int verbosity) {
    while (is_running) {
        char buffer[4096];

        ssize_t bytes_read = stream.read(buffer, sizeof(buffer));

        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // timeout
                log_message(verbosity, VerbosityLevel::NON_CRITICAL, "timeout: no data received. reconnecting...");

                return StreamResult::TIMEOUT;
            }

            throw std::runtime_error("audio read error");
        }

        if (bytes_read == 0) {
            log_message(verbosity, VerbosityLevel::COMMON, "connection closed by server");

            return StreamResult::CLOSED_BY_SERVER;
        }

        std::cout.write(buffer, bytes_read);

        if (!std::cout) {
            throw std::runtime_error("failed to write audio to stdout");
        }

        log_message(verbosity, VerbosityLevel::DEBUG,  "####DEBUG#### audio: wrote " + std::to_string(bytes_read) + " bytes");
    }

    log_message(verbosity, VerbosityLevel::COMMON, "client requested shutdown");

    return StreamResult::STOPPED_BY_CLIENT;
}

static StreamResult handle_metadata(IStream& stream, std::atomic<bool>& is_running, const size_t metaint, const int verbosity) {
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
                log_message(verbosity, VerbosityLevel::NON_CRITICAL, "timeout: no data received. reconnecting...");

                return StreamResult::TIMEOUT;
            }

            throw std::runtime_error("audio read error");
        }
        if (bytes_read == 0) {
            log_message(verbosity, VerbosityLevel::COMMON, "connection closed by server");

            return StreamResult::CLOSED_BY_SERVER;
        }

        bytes_to_read -= static_cast<size_t>(bytes_read);

        switch (state) {
            case StreamState::AUDIO:
                std::cout.write(buffer, bytes_read);

                if (!std::cout) {
                    throw std::runtime_error("failed to write auto do stdout");
                }

                log_message(verbosity, VerbosityLevel::DEBUG,"####DEBUG#### audio: wrote " + std::to_string(bytes_read) + " bytes");

                if (bytes_to_read == 0) {
                    state = StreamState::MULTIPLIER;
                    bytes_to_read = 1;
                }
                break;

            case StreamState::MULTIPLIER: {
                unsigned char k = static_cast<unsigned char>(buffer[0]);
                size_t metadata_length = k * 16;

                log_message(verbosity, VerbosityLevel::DEBUG, "####DEBUG#### metadata block: k=" + std::to_string(k) + " length=" + std::to_string(metadata_length));

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

                        log_message(verbosity, VerbosityLevel::DEBUG, "####DEBUG#### metadata payload size=" + std::to_string(clean_meta.size()) + " bytes");
                    }

                    // go back to listening to music
                    state = StreamState::AUDIO;
                    bytes_to_read = metaint;
                }

                break;
        }
    }

    log_message(verbosity, VerbosityLevel::COMMON, "client requested shutdown");

    return StreamResult::STOPPED_BY_CLIENT;
}

static StreamResult listen_to_music(IStream& stream, std::atomic<bool>& is_running, const size_t metaint, const int verbosity) {
    if (metaint == 0) {
        log_message(verbosity, VerbosityLevel::DEBUG, "####DEBUG#### mode: no metadata (raw audio)");

        return handle_no_metadata(stream, is_running, verbosity);
    }

    log_message(verbosity, VerbosityLevel::DEBUG, "####DEBUG#### mode: metadata interleaved every "
        + std::to_string(metaint) + " bytes");

    return handle_metadata(stream, is_running, metaint, verbosity);
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
    ClientConfig config;

    try {
        config = parse_arguments(argc, argv);
        if (!parseUrl(config.server_url).has_value()) {
            throw std::invalid_argument("invalid URL format: " + config.server_url);
        }

        log_message(
            config.verbosity, VerbosityLevel::DEBUG,
            "####DEBUG#### config: url=" + config.server_url
            + " timeout=" + std::to_string(config.timeout)
            + " verbosity=" + std::to_string(config.verbosity)
        );

        std::string current_url = config.server_url;
        std::string current_cookie = "";

        std::unique_ptr<IStream> stream;

        while (is_running) {
            auto parsed_opt = parseUrl(current_url);

            if (!parsed_opt.has_value()) {
                throw std::runtime_error("invalid URL format: " + current_url);
            }

            ParsedURL const parsed_url = parsed_opt.value();

            log_message(
                config.verbosity, VerbosityLevel::DEBUG,
                "####DEBUG#### parsed URL: host=" + parsed_url.hostname
                + " port=" + parsed_url.port_str
                + " path=" + parsed_url.path
            );

            stream = connect_to_server(parsed_url, config);

            send_http_request(*stream, parsed_url, config, current_cookie);

            HeaderReadResult header_result = server_response_to_text(*stream, config.verbosity);

            if (header_result.result == StreamResult::TIMEOUT) {
                log_message(config.verbosity, VerbosityLevel::DEBUG, "####DEBUG#### header read result: TIMEOUT");
                continue;
            }

            if (header_result.result == StreamResult::CLOSED_BY_SERVER) {
                log_message(config.verbosity, VerbosityLevel::DEBUG, "####DEBUG#### header read result: CLOSED_BY_SERVER");
                break;
            }

            const std::string server_response_text = header_result.text;
            log_message(config.verbosity, VerbosityLevel::COMMON, server_response_text);

            auto response_opt = process_http_response(server_response_text, config.verbosity);
            if (!response_opt.has_value()) {
                throw std::runtime_error("invalid response");
            }

            const HttpResponseData response_data = response_opt.value();
            if (response_data.critical_error) {
                throw std::runtime_error("critical error " +  std::to_string(response_data.status_code));
            }

            if (response_data.status_code == 200) {
                StreamResult stream_result = listen_to_music(*stream, is_running, response_data.icy_metaint, config.verbosity);
                log_message(config.verbosity, VerbosityLevel::DEBUG, "####DEBUG#### listen_to_music result: " + stream_result_to_string(stream_result));

                if (stream_result == StreamResult::TIMEOUT) {
                    continue;
                }

                if (stream_result == StreamResult::CLOSED_BY_SERVER || stream_result == StreamResult::STOPPED_BY_CLIENT) {
                    break;
                }
            } else if (response_data.status_code == 301 || response_data.status_code == 302) {
                // redirect
                if (response_data.new_location.empty()) {
                    throw std::runtime_error("client redirected to nonexistent location");
                }

                current_url = response_data.new_location;
                log_message(config.verbosity, VerbosityLevel::DEBUG, "####DEBUG#### current_url updated to: " + current_url);

                if (!response_data.cookie.empty()) {
                    if (!current_cookie.empty()) {
                        current_cookie += "; ";
                    }

                    current_cookie += response_data.cookie;

                    log_message(config.verbosity, VerbosityLevel::DEBUG, "####DEBUG#### current_cookie updated");
                }

                log_message(config.verbosity, VerbosityLevel::COMMON, "redirecting to " + current_url, true);
                continue;
            } else {
                // TODO: zmien to
                throw std::runtime_error("unsuported status code " + std::to_string(response_data.status_code));
            }
        }
    } catch (const std::invalid_argument& e) {
        is_running = false;
        std::cerr << "ERROR: " << e.what() << std::endl;
        exit_code = EXIT_FAILURE;
    } catch (const std::exception& e) {
        if (!is_running) {
            exit_code = EXIT_SUCCESS;
        } else {
            is_running = false;

            if (config.verbosity >= 2) {
                std::cerr << get_current_timestamp() << "\n";
                std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
            }

            exit_code = EXIT_FAILURE;
        }
    }

    is_running = false;

    if (input_thread.joinable()) {
        // wait for the thread to finish
        input_thread.join();
    }

    return exit_code;
}