#include "http_logic.h"

#include <algorithm>
#include <iostream>
#include <sstream>

#include "logger.h"

namespace {
    constexpr int HTTP_STATUS_OK = 200;
    constexpr int HTTP_STATUS_REDIRECT_MIN = 300;
    constexpr int HTTP_STATUS_REDIRECT_MAX = 399;
    constexpr size_t MAX_HEADERS_SIZE = 64 * 1024; // 64 KB limit to prevent infinite loops
}

/**
 * @brief Guarantees that all bytes of the request are written to the stream.
 * @param stream The connection stream.
 * @param data Pointer to the buffer.
 * @param length Number of bytes to write.
 * @param verbosity Configured verbosity level for logging.
 */
static void write_all(IStream &stream, const char *data, size_t length, const int verbosity) {
    size_t written_total = 0;

    while (written_total < length) {
        const ssize_t written = stream.write(data + written_total, length - written_total);

        if (written < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }

            throw std::runtime_error("failed to write request to socket");
        }

        if (written == 0) {
            throw std::runtime_error("socket write returned 0");
        }

        written_total += static_cast<size_t>(written);

        log_message(verbosity, VerbosityLevel::DEBUG, "####DEBUG#### request write progress: " +
                                                      std::to_string(written_total) + "/" + std::to_string(length) +
                                                      " bytes");
    }
}

/**
 * @brief Parses HTTP headers specifically for a 200 OK response (extracts icy-metaint).
 */
static void handle_200_ok(std::istringstream &stream, HttpResponseData &response, const int verbosity) {
    std::string current_line;

    while (std::getline(stream, current_line)) {
        if (!current_line.empty() && current_line.back() == '\r') {
            current_line.pop_back();
        }

        if (current_line.empty()) {
            // end of the text
            break;
        }

        const size_t colon_pos = current_line.find(':');
        if (colon_pos == std::string::npos) {
            // malformed header (according to the http standard)
            log_message(verbosity, VerbosityLevel::NON_CRITICAL, "malformed HTTP header ignored.");
            continue;
        }

        std::string key = current_line.substr(0, colon_pos);
        std::string value = current_line.substr(colon_pos + 1);

        // case insensitivity on the key
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        if (key == "icy-metaint") {
            try {
                response.icy_metaint = std::stoull(value);

                size_t pos = 0;
                response.icy_metaint = std::stoull(value, &pos);

                if (pos != value.find_last_not_of(" \t\r\n") + 1) {
                    throw std::invalid_argument("trailing characters");
                }

                log_message(verbosity, VerbosityLevel::DEBUG,
                            "####DEBUG#### icy-metaint parsed: " + std::to_string(response.icy_metaint));
            } catch (...) {
                response.icy_metaint = 0;

                log_message(verbosity, VerbosityLevel::NON_CRITICAL, "invalid icy-metaint value. metadata disabled.");
            }
        }
    }
}

/**
 * @brief Parses HTTP headers specifically for a 3xx Redirect response (extracts Location and Set-Cookie).
 */
static void handle_redirect(std::istringstream &stream, HttpResponseData &response, const int verbosity) {
    std::string current_line;

    while (std::getline(stream, current_line)) {
        if (!current_line.empty() && current_line.back() == '\r') {
            current_line.pop_back();
        }

        if (current_line.empty()) {
            break;
        }

        const size_t colon_pos = current_line.find(':');
        if (colon_pos == std::string::npos) {
            // malformed header (according to the http standard)
            log_message(verbosity, VerbosityLevel::NON_CRITICAL, "malformed HTTP header ignored.");
            continue;
        }

        std::string key = current_line.substr(0, colon_pos);
        std::string value = current_line.substr(colon_pos + 1);

        // case insensitivity on the key
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        if (key == "location") {
            const size_t first_non_space = value.find_first_not_of(' ');
            if (first_non_space != std::string::npos) {
                response.new_location = value.substr(first_non_space);

                log_message(verbosity, VerbosityLevel::DEBUG,
                            "####DEBUG#### redirect Location parsed: " + response.new_location);
            }
        } else if (key == "set-cookie") {
            const size_t first_non_space = value.find_first_not_of(" \t");
            const size_t semicolon_pos = value.find(';', first_non_space);

            if (first_non_space == std::string::npos) {
                log_message(verbosity, VerbosityLevel::NON_CRITICAL,
                            "empty Set-Cookie header ignored.");
                continue;
            }

            std::string cookie_value;
            if (semicolon_pos == std::string::npos) {
                cookie_value = value.substr(first_non_space);
            } else {
                cookie_value = value.substr(first_non_space,
                                            semicolon_pos - first_non_space);
            }

            const size_t last_non_space = cookie_value.find_last_not_of(" \t");
            if (last_non_space != std::string::npos) {
                cookie_value = cookie_value.substr(0, last_non_space + 1);
            }

            if (!cookie_value.empty()) {
                response.cookies.push_back(cookie_value);
            }

            log_message(verbosity, VerbosityLevel::DEBUG,
                        "####DEBUG#### cookie parsed");
        }

        log_message(verbosity, VerbosityLevel::DEBUG, "####DEBUG#### cookie parsed");
    }
}

std::string build_http_request(const ParsedURL &parsed_url, const ClientConfig &config,
                               const std::string &current_cookie) {
    std::string request = "GET " + parsed_url.path + " HTTP/1.1\r\n";

    std::string host_header = parsed_url.hostname;

    const bool is_ipv6_literal = host_header.find(':') != std::string::npos;
    if (is_ipv6_literal) {
        host_header = "[" + host_header + "]";
    }

    const bool non_default_port =
            (parsed_url.protocol == Protocol::HTTP &&
             parsed_url.port_str != DefaultPort::HTTP) ||
            (parsed_url.protocol == Protocol::HTTPS &&
             parsed_url.port_str != DefaultPort::HTTPS);

    if (non_default_port) {
        host_header += ":" + parsed_url.port_str;
    }

    request += "Host: " + host_header + "\r\n";
    request += "Connection: Keep-Alive\r\n";

    if (config.request_metadata) {
        request += "Icy-MetaData: 1\r\n";
    }

    if (!current_cookie.empty()) {
        request += "Cookie: " + current_cookie + "\r\n";
    }

    request += "\r\n";

    return request;
}

void send_http_request(IStream &stream, const ParsedURL &parsed_url, const ClientConfig &config,
                       const std::string current_cookie) {
    const std::string request = build_http_request(parsed_url, config, current_cookie);

    write_all(stream, request.data(), request.length(), config.verbosity);

    log_message(config.verbosity, VerbosityLevel::COMMON, request);
}

HeaderReadResult server_response_to_text(IStream &stream, const int verbosity) {
    char c;
    std::string received_text;

    while (true) {
        ssize_t bytes_read = stream.read(&c, 1);

        if (bytes_read > 0) {
            received_text += c;

            if (received_text.size() > MAX_HEADERS_SIZE) {
                throw std::runtime_error("HTTP headers too large");
            }

            if (received_text.ends_with("\r\n\r\n")) {
                log_message(verbosity, VerbosityLevel::DEBUG,
                            "####DEBUG#### received HTTP headers size=" + std::to_string(received_text.size()) +
                            " bytes");
                return HeaderReadResult{StreamResult::OK, received_text};
            }
        } else if (bytes_read == 0) {
            if (!received_text.empty()) {
                throw std::runtime_error("incomplete HTTP headers");
            }

            return HeaderReadResult{StreamResult::CLOSED_BY_SERVER, ""};
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // timeout
                log_message(verbosity, VerbosityLevel::NON_CRITICAL, "timeout waiting for server response.");

                return HeaderReadResult{StreamResult::TIMEOUT, ""};
            }

            throw std::runtime_error("failed to read the message received from the server.");
        }
    }
}

std::optional<HttpResponseData> process_http_response(const std::string &headers_text, const int verbosity) {
    if (headers_text.empty()) {
        // no response from the server
        return std::nullopt;
    }

    HttpResponseData response;
    std::istringstream main_stream(headers_text);

    // find code status
    std::string first_line;

    if (!std::getline(main_stream, first_line)) {
        // empty or malformed
        return std::nullopt;
    }

    // a smaller stream to get only the first line to facilitate extracting code status
    std::istringstream line_stream(first_line);
    std::string protocol;

    if (!(line_stream >> protocol >> response.status_code)) {
        // if extracting a number is not possible
        return std::nullopt;
    }

    log_message(verbosity, VerbosityLevel::DEBUG,
                "####DEBUG#### HTTP status parsed: protocol=" + protocol + " status=" + std::to_string(
                    response.status_code));

    if (response.status_code == HTTP_STATUS_OK) {
        handle_200_ok(main_stream, response, verbosity);
    } else if (response.status_code >= HTTP_STATUS_REDIRECT_MIN && response.status_code <= HTTP_STATUS_REDIRECT_MAX) {
        handle_redirect(main_stream, response, verbosity);
    } else {
        response.critical_error = true;
    }

    return response;
}
