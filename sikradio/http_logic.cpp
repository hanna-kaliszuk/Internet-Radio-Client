#include "http_logic.h"

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
