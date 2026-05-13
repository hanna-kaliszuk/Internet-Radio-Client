#pragma once

#include <string>
#include "client_config.h"
#include "network_logic.h"

struct HttpResponseData {
    int status_code = 0;
    std::string new_location;
    std::string cookie;
    size_t icy_metaint = 0;
    bool critical_error = false;
};

std::string build_http_request(const ParsedURL& parsed_url, const ClientConfig& config, const std::string& current_cookie = "");

void send_http_request(const int socket_fd, const ParsedURL& parsed_url, const ClientConfig& config, const std::string current_cookie = "");

std::optional<std::string> server_response_to_text();

std::optional<HttpResponseData> parse_http_response(const std::string& headers_text);