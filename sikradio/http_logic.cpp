#include "http_logic.h"

#include <unistd.h>
#include <iostream>

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

    // TODO: wypisywanie logów w zależnośc

}
