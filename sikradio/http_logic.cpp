#include "http_logic.h"
#include "logger.h"

#include <algorithm>
#include <unistd.h>
#include <iostream>
#include <sstream>

static void write_all(IStream& stream, const char* data, size_t length, const int verbosity) {
    size_t written_total = 0;

    while (written_total < length) {
        ssize_t written = stream.write(data + written_total, length - written_total);

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
            std::to_string(written_total) + "/" + std::to_string(length) + " bytes");
    }
}

static void handle_200_ok(std::istringstream& stream, HttpResponseData& response, const int verbosity) {
    std::string current_line;

    while (std::getline(stream, current_line)) {
        if (!current_line.empty() && current_line.back() == '\r') {
            // delete '\r'
            current_line.pop_back();
        }

        if (current_line.empty()) {
            // end of the text
            break;
        }

        size_t colon_pos = current_line.find(':');
        if (colon_pos == std::string::npos) {
            // malformed header (according to the http standard)
            log_message(verbosity, VerbosityLevel::NON_CRITICAL, "malformed HTTP header ignored.");
            continue;
        }

        std::string key = current_line.substr(0, colon_pos);
        std::string value = current_line.substr(colon_pos + 1);

        // case insensivity on the key
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        if (key == "icy-metaint") {
            try {
                response.icy_metaint = std::stoull(value);

                size_t pos = 0;
                response.icy_metaint = std::stoull(value, &pos);

                if (pos != value.find_last_not_of(" \t\r\n") + 1) {
                    throw std::invalid_argument("trailing characters");
                }

                log_message(verbosity, VerbosityLevel::DEBUG, "####DEBUG#### icy-metaint parsed: " + std::to_string(response.icy_metaint));
            } catch (...) {
                response.icy_metaint = 0;

                log_message(verbosity, VerbosityLevel::NON_CRITICAL, "invalid icy-metaint value. metadata disabled.");
            }
        }

    }
}

static void handle_redirect(std::istringstream& stream, HttpResponseData& response, const int verbosity) {
    std::string current_line;

    while (std::getline(stream, current_line)) {
        if (!current_line.empty() && current_line.back() == '\r') {
            current_line.pop_back();
        }

        if (current_line.empty()) {
            break;
        }

        size_t colon_pos = current_line.find(':');
        if (colon_pos == std::string::npos) {
            // malformed header (according to the http standard)
            log_message(verbosity, VerbosityLevel::NON_CRITICAL, "malformed HTTP header ignored.");
            continue;
        }

        std::string key = current_line.substr(0, colon_pos);
        std::string value = current_line.substr(colon_pos + 1);

        // case insensivity on the key
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        if (key == "location") {
            const size_t first_non_space = value.find_first_not_of(' ');
            if (first_non_space != std::string::npos) {
                response.new_location = value.substr(first_non_space);

                log_message(verbosity, VerbosityLevel::DEBUG, "####DEBUG#### redirect Location parsed: " + response.new_location);
            }
        } else if (key == "set-cookie") {
            const size_t first_non_space = value.find_first_not_of(' ');
            const size_t semicolon_pos = value.find_first_of(';');

            if (first_non_space == std::string::npos) {
                // no cookies found
                    log_message(verbosity, VerbosityLevel::NON_CRITICAL,"empty Set-Cookie header ignored.");
                    continue;
            }

            log_message(verbosity, VerbosityLevel::DEBUG, "####DEBUG#### cookie parsed");

            if (semicolon_pos == std::string::npos) {
                response.cookie = value.substr(first_non_space);
            } else {
                response.cookie = value.substr(first_non_space, semicolon_pos - first_non_space);
            }
        }
    }
}

std::string build_http_request(const ParsedURL& parsed_url, const ClientConfig& config, const std::string& current_cookie) {
    std::string request = "GET " + parsed_url.path + " HTTP/1.1\r\n";
    std::string host_hdr = parsed_url.hostname;

    if (host_hdr.find(':') != std::string::npos && host_hdr.front() != '[') {
        host_hdr = "[" + host_hdr + "]";
    }

    if (!parsed_url.port_str.empty()) {
        host_hdr += ":" + parsed_url.port_str;
    }

    request += "Host: " + host_hdr + "\r\n";
    request += "Connection: Keep-Alive\r\n";

    if (!current_cookie.empty()) {
        // if there is a required cookie
        request += "Cookie: " + current_cookie + "\r\n";
    }

    if (config.request_metadata) {
        request += "Icy-MetaData: 1\r\n";
    }

    request += "\r\n";

    return request;
}

void send_http_request(IStream& stream, const ParsedURL& parsed_url, const ClientConfig& config, const std::string current_cookie){
    std::string request = build_http_request(parsed_url, config, current_cookie);

    write_all(stream, request.data(), request.length(), config.verbosity);

    log_message(config.verbosity, VerbosityLevel::COMMON, request);
}

// wczytuje to co wyslal serwer bit po bicie az do dojscia do \r\n\r\n
HeaderReadResult server_response_to_text(IStream& stream, const int verbosity) {
    static constexpr size_t MAX_HEADERS_SIZE = 64 * 1024; // zeby czytanie kiedys sie zatrzymalo jakby byl zlosliwy serwer

    char c;

    std::string received_text;

    while (true) {
        ssize_t bytes_read = stream.read(&c, 1);

        if (bytes_read > 0) {
            // read a letter
            received_text += c;

            if (received_text.size() > MAX_HEADERS_SIZE) {
                throw std::runtime_error("HTTP headers too large");
            }

            if (received_text.ends_with("\r\n\r\n")) {
                log_message(verbosity, VerbosityLevel::DEBUG, "####DEBUG#### received HTTP headers size=" + std::to_string(received_text.size()) + " bytes");
                return HeaderReadResult{StreamResult::OK, received_text};
            }
        } else if (bytes_read == 0) {
            return HeaderReadResult{StreamResult::CLOSED_BY_SERVER, ""};
        } else {
            // bytes read < 0 => check errno
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

    log_message(verbosity, VerbosityLevel::DEBUG,"####DEBUG#### HTTP status parsed: protocol=" + protocol + " status=" + std::to_string(response.status_code));

    switch (response.status_code) {
        case 200:
            handle_200_ok(main_stream, response, verbosity);
            break;
        case 301:
        case 302:
            handle_redirect(main_stream, response, verbosity);
            break;
        default:
            response.critical_error = true;
            break;
    }

    return response;
}
