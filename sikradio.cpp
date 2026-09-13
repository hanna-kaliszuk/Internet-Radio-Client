#include "client_config.h"
#include "http_logic.h"
#include "IStream.h"
#include "logger.h"
#include "network_logic.h"
#include "url_parser.h"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <iostream>
#include <map>
#include <thread>

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace {
    constexpr size_t AUDIO_BUFFER_SIZE = 4096;
    constexpr size_t STDIN_BUFFER_SIZE = 128;
    constexpr size_t METADATA_BLOCK_MULTIPLIER = 16;
    constexpr size_t METADATA_LENGTH_BYTE_SIZE = 1;
    constexpr int POLL_TIMEOUT_MS = 100;

    constexpr int HTTP_STATUS_OK = 200;
    constexpr int HTTP_STATUS_REDIRECT_MIN = 300;
    constexpr int HTTP_STATUS_REDIRECT_MAX = 399;
}

/**
 * @brief Trims leading and trailing whitespaces from a string.
 */
static std::string trim(const std::string &s) {
    const size_t begin = s.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return "";
    }

    const size_t end = s.find_last_not_of(" \t");
    return s.substr(begin, end - begin + 1);
}

/**
 * @brief Parses a raw cookie string and stores it in the cookies map.
 */
static void store_cookie(std::map<std::string, std::string> &cookies,
                         const std::string &cookie_pair) {
    const std::string cleaned = trim(cookie_pair);
    if (cleaned.empty()) {
        return;
    }

    const size_t eq_pos = cleaned.find('=');
    if (eq_pos == std::string::npos) {
        return;
    }

    const std::string name = trim(cleaned.substr(0, eq_pos));
    const std::string value = trim(cleaned.substr(eq_pos + 1));

    if (name.empty()) {
        return;
    }

    cookies[name] = value;
}

/**
 * @brief Constructs a single Cookie header string from the map of cookies.
 */
static std::string build_cookie_header(const std::map<std::string, std::string> &cookies) {
    std::string result;

    for (const auto &[name, value]: cookies) {
        if (!result.empty()) {
            result += "; ";
        }

        result += name + "=" + value;
    }

    return result;
}

/**
 * @brief Converts a StreamResult enum to its string representation (for debugging).
 */
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

/**
 * @brief Reads audio data endlessly (without ICY demultiplexing) and writes it to stdout.
 */
static StreamResult handle_no_metadata(IStream &stream, const std::atomic<bool> &is_running, const int verbosity) {
    while (is_running) {
        char buffer[AUDIO_BUFFER_SIZE];

        const ssize_t bytes_read = stream.read(buffer, sizeof(buffer));

        if (bytes_read < 0) {
            if (!is_running) {
                log_message(verbosity, VerbosityLevel::COMMON, "client requested shutdown");

                return StreamResult::STOPPED_BY_CLIENT;
            }
            
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                // timeout
                log_message(verbosity, VerbosityLevel::COMMON, "data receiving timeout. trying again.");

                return StreamResult::TIMEOUT;
            }

            throw std::runtime_error("audio read error");
        }

        if (bytes_read == 0) {
            if (!is_running) {
                log_message(verbosity, VerbosityLevel::COMMON, "client requested shutdown");

                return StreamResult::STOPPED_BY_CLIENT;
            }

            log_message(verbosity, VerbosityLevel::COMMON, "connection closed by server");

            return StreamResult::CLOSED_BY_SERVER;
        }

        std::cout.write(buffer, bytes_read);

        if (!std::cout) {
            throw std::runtime_error("failed to write audio to stdout");
        }

        log_message(verbosity, VerbosityLevel::DEBUG,
                    "####DEBUG#### audio: wrote " + std::to_string(bytes_read) + " bytes");
    }

    log_message(verbosity, VerbosityLevel::COMMON, "client requested shutdown");

    return StreamResult::STOPPED_BY_CLIENT;
}

/**
 * @brief Demultiplexes an ICY stream, separating audio chunks from metadata blocks.
 */
static StreamResult handle_metadata(IStream &stream, const std::atomic<bool> &is_running, const size_t metaint,
                                    const int verbosity) {
    char buffer[AUDIO_BUFFER_SIZE];

    StreamState state = StreamState::AUDIO;
    size_t bytes_to_read = metaint; // starting with the audio data
    std::string metadata_buffer;

    while (is_running) {
        // read only as many bytes as needed for this specific state
        const size_t chunk_size = std::min(sizeof(buffer), bytes_to_read);
        const ssize_t bytes_read = stream.read(buffer, chunk_size);

        if (bytes_read < 0) {
            if (!is_running) {
                log_message(verbosity, VerbosityLevel::COMMON, "client requested shutdown");

                return StreamResult::STOPPED_BY_CLIENT;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                // timeout
                log_message(verbosity, VerbosityLevel::COMMON, "data receiving timeout. trying again.");

                return StreamResult::TIMEOUT;
            }

            throw std::runtime_error("audio read error");
        }
        if (bytes_read == 0) {
            if (!is_running) {
                log_message(verbosity, VerbosityLevel::COMMON, "client requested shutdown");

                return StreamResult::STOPPED_BY_CLIENT;
            }

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

                log_message(verbosity, VerbosityLevel::DEBUG,
                            "####DEBUG#### audio: wrote " + std::to_string(bytes_read) + " bytes");

                if (bytes_to_read == 0) {
                    state = StreamState::MULTIPLIER;
                    bytes_to_read = METADATA_LENGTH_BYTE_SIZE;
                }
                break;

            case StreamState::MULTIPLIER: {
                const unsigned char k = static_cast<unsigned char>(buffer[0]);
                const size_t metadata_length = k * METADATA_BLOCK_MULTIPLIER;

                log_message(verbosity, VerbosityLevel::DEBUG,
                            "####DEBUG#### metadata block: k=" + std::to_string(k) + " length=" + std::to_string(
                                metadata_length));

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

                    for (char c: metadata_buffer) {
                        if (c != '\0') {
                            clean_meta += c;
                        }
                    }

                    if (!clean_meta.empty()) {
                        std::cerr << clean_meta << "\n";

                        log_message(verbosity, VerbosityLevel::DEBUG,
                                    "####DEBUG#### metadata payload size=" + std::to_string(clean_meta.size()) +
                                    " bytes");
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

/**
 * @brief Routes to the correct streaming strategy based on metaint parameter.
 */
static StreamResult listen_to_music(IStream &stream, std::atomic<bool> &is_running, const size_t metaint,
                                    const int verbosity) {
    if (metaint == 0) {
        log_message(verbosity, VerbosityLevel::DEBUG, "####DEBUG#### mode: no metadata (raw audio)");

        return handle_no_metadata(stream, is_running, verbosity);
    }

    log_message(verbosity, VerbosityLevel::DEBUG, "####DEBUG#### mode: metadata interleaved every "
                                                  + std::to_string(metaint) + " bytes");

    return handle_metadata(stream, is_running, metaint, verbosity);
}

/**
 * @brief Initializes the global OpenSSL environment context.
 */
static void initialize_open_ssl() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
}

int main(int argc, char *argv[]) {
    initialize_open_ssl();
    std::atomic<bool> is_running{true};
    std::atomic<int> current_fd{-1};

    // a thread dedicated to monitoring STDIN for the "quit\n" sequence
    std::thread input_thread([&is_running, &current_fd]() {
        struct pollfd pfd;
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;

        std::string window;
        constexpr std::string_view quit_sequence = "quit\n";

        while (is_running) {
            pfd.revents = 0;

            const int ret = poll(&pfd, 1, POLL_TIMEOUT_MS);

            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }

            if (ret == 0) {
                continue;
            }

            // read available data before breaking
            if (pfd.revents & POLLIN) {
                char buffer[STDIN_BUFFER_SIZE];
                const ssize_t bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer));

                if (bytes_read < 0) {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                    break;
                }

                if (bytes_read == 0) {
                    break;
                }

                for (ssize_t i = 0; i < bytes_read; ++i) {
                    window += buffer[i];

                    if (window.size() > quit_sequence.size()) {
                        window.erase(0, window.size() - quit_sequence.size());
                    }

                    if (window == quit_sequence) {
                        is_running = false;

                        const int fd = current_fd.load();
                        if (fd != -1) {
                            shutdown(fd, SHUT_RDWR);
                        }

                        break;
                    }
                }
            } else if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
                break;
            }
        }
    });

    int exit_code = EXIT_SUCCESS;
    ClientConfig config;

    try {
        config = parse_arguments(argc, argv);
        if (!parse_url(config.server_url).has_value()) {
            throw std::invalid_argument("invalid URL format: " + config.server_url);
        }

        log_message(
            config.verbosity, VerbosityLevel::DEBUG,
            "####DEBUG#### config: url=" + config.server_url
            + " timeout=" + std::to_string(config.timeout)
            + " verbosity=" + std::to_string(config.verbosity)
        );

        std::string current_url = config.server_url;
        std::map<std::string, std::string> current_cookies;
        std::unique_ptr<IStream> stream;

        while (is_running) {
            auto parsed_opt = parse_url(current_url);

            if (!parsed_opt.has_value()) {
                throw std::runtime_error("invalid URL format: " + current_url);
            }

            const ParsedURL parsed_url = parsed_opt.value();

            log_message(
                config.verbosity, VerbosityLevel::DEBUG,
                "####DEBUG#### parsed URL: host=" + parsed_url.hostname
                + " port=" + parsed_url.port_str
                + " path=" + parsed_url.path
            );

            stream = connect_to_server(parsed_url, config);
            current_fd = stream->get_fd();

            send_http_request(*stream, parsed_url, config, build_cookie_header(current_cookies));

            const HeaderReadResult header_result = server_response_to_text(*stream, config.verbosity);

            if (header_result.result == StreamResult::TIMEOUT) {
                log_message(config.verbosity, VerbosityLevel::DEBUG, "####DEBUG#### header read result: TIMEOUT");
                stream->close();
                current_fd = -1;
                current_url = config.server_url;
                current_cookies.clear();
                continue;
            }

            if (header_result.result == StreamResult::CLOSED_BY_SERVER) {
                log_message(config.verbosity, VerbosityLevel::DEBUG,
                            "####DEBUG#### header read result: CLOSED_BY_SERVER");
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
                throw std::runtime_error("critical error " + std::to_string(response_data.status_code));
            }

            if (response_data.status_code == HTTP_STATUS_OK) {
                const size_t metaint = config.request_metadata ? response_data.icy_metaint : 0;
                const StreamResult stream_result = listen_to_music(*stream, is_running, metaint, config.verbosity);

                log_message(config.verbosity, VerbosityLevel::DEBUG,
                            "####DEBUG#### listen_to_music result: " + stream_result_to_string(stream_result));

                if (stream_result == StreamResult::TIMEOUT) {
                    stream->close();
                    current_fd = -1;
                    current_url = config.server_url;
                    current_cookies.clear();

                    continue;
                }

                if (stream_result == StreamResult::STOPPED_BY_CLIENT) {
                    exit_code = EXIT_SUCCESS;
                    stream->close();
                    current_fd = -1;

                    break;
                }

                if (stream_result == StreamResult::CLOSED_BY_SERVER) {
                    exit_code = EXIT_SUCCESS;
                    stream->close();
                    current_fd = -1;

                    break;
                }
            } else if (response_data.status_code >= HTTP_STATUS_REDIRECT_MIN && response_data.status_code <= HTTP_STATUS_REDIRECT_MAX) {
                // redirect
                if (response_data.new_location.empty()) {
                    throw std::runtime_error("client redirected to nonexistent location");
                }

                current_url = response_data.new_location;

                log_message(config.verbosity, VerbosityLevel::DEBUG,
                            "####DEBUG#### current_url updated to: " + current_url);

                for (const std::string &cookie: response_data.cookies) {
                    store_cookie(current_cookies, cookie);
                }

                if (!response_data.cookies.empty()) {
                    log_message(config.verbosity, VerbosityLevel::DEBUG,
                                "####DEBUG#### current cookies updated");
                }

                log_message(config.verbosity, VerbosityLevel::COMMON, "redirecting to " + current_url, true);
                continue;
            } else {
                throw std::runtime_error("unsupported status code " + std::to_string(response_data.status_code));
            }
        }
    } catch (const std::invalid_argument &e) {
        if (!is_running) {
            exit_code = EXIT_SUCCESS;
        } else {
            is_running = false;
            std::cerr << "ERROR: " << e.what() << std::endl;
            exit_code = EXIT_FAILURE;
        }
    } catch (const std::exception &e) {
        if (!is_running) {
            exit_code = EXIT_SUCCESS;
        } else {
            is_running = false;

            if (config.verbosity >= static_cast<int>(VerbosityLevel::CRITICAL)) {
                std::cerr << get_current_timestamp() << "\n";
                std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
            }

            exit_code = EXIT_FAILURE;
        }
    }

    is_running = false;

    const int fd = current_fd.load();
    if (fd != -1) {
        shutdown(fd, SHUT_RDWR);
    }

    if (input_thread.joinable()) {
        // wait for the thread to finish
        input_thread.join();
    }

    return exit_code;
}
