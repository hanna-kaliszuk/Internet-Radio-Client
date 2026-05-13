#include "http_logic.h"

#include <unistd.h>
#include <iostream>
#include <sstream>

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

void send_http_request(const int socket_fd, const ParsedURL& parsed_url, const ClientConfig& config, const std::string& current_cookie) {
    std::string request = build_http_request(parsed_url, config, current_cookie);

    ssize_t const bytes_written = write(socket_fd, request.data(), request.length());

    if (bytes_written < 0 || static_cast<size_t>(bytes_written) != request.length()) {
        throw std::runtime_error("failed to write HTTP request to socket");
    }

    // TODO: wypisywanie logów w zależności od ustawionego verbosity

}

std::optional<HttpResponseData> parse_http_response(const std::string &headers_text) {
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

    // TODO: do oddelegowania do mniejszych funkcji w zaleznosci od tego jaki je
}
