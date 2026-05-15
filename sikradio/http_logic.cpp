#include "http_logic.h"

#include <algorithm>
#include <unistd.h>
#include <iostream>
#include <sstream>

static void handle_200_ok(std::istringstream& stream, HttpResponseData& response) {
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
            continue;
        }

        std::string key = current_line.substr(0, colon_pos);
        std::string value = current_line.substr(colon_pos + 1);

        // case insensivity on the key
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        if (key == "icy-metaint") {
            try {
                response.icy_metaint = std::stoull(value);
            } catch (...) {
                response.icy_metaint = 0;
            }
        }

    }
}

static void handle_redirect(std::istringstream& stream, HttpResponseData& response) {
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
            }
        } else if (key == "set-cookie") {
            const size_t first_non_space = value.find_first_not_of(' ');
            const size_t semicolon_pos = value.find_first_of(';');

            if (first_non_space == std::string::npos) {
                // no cookies fou
                break;
            }

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
    request += "Host: " + parsed_url.hostname + "\r\n";
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

    ssize_t const bytes_written = stream.write(request.data(), request.length());

    if (bytes_written < 0 || static_cast<size_t>(bytes_written) != request.length()) {
        throw std::runtime_error("failed to write request to socket");
    }

    // TODO: wypisywanie logów w zależności od ustawionego verbosity

}

// wczytuje to co wyslal serwer bit po bicie az do dojscia do \r\n\r\n
std::optional<std::string> server_response_to_text(IStream& stream) {
    char c;
    ssize_t bytes_read = 0;

    std::string received_text;

    while (true) {
        bytes_read = stream.read(&c, 1);

        if (bytes_read > 0) {
            // read a letter
            received_text += c;
            if (received_text.ends_with("\r\n\r\n")) {
                break;
            }
        } else if (bytes_read == 0) {
            throw ConnectionClosedException();
        } else {
            // bytes read < 0 => check errno
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // timeout
                // TODO: moze dodac tutaj printa, ze timeout

                return std::nullopt;
            }

            throw std::runtime_error("failed to read the message received from the server.");
        }
    }

    return received_text;
}

std::optional<HttpResponseData> process_http_response(const std::string &headers_text) {
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

    switch (response.status_code) {
        case 200:
            handle_200_ok(main_stream, response);
            break;
        case 301:
        case 302:
            handle_redirect(main_stream, response);
            break;
        default:
            response.critical_error = true;
            break;
    }

    return response;
}
