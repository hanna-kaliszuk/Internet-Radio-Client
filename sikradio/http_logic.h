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

// wyjątek oznaczający czyste zamknięcie połączenia przez serwer
class ConnectionClosedException : public std::exception {
public:
    const char* what() const noexcept override {
        return "Connection closed by server (EOF)";
    }
};

std::string build_http_request(const ParsedURL& parsed_url, const ClientConfig& config, const std::string& current_cookie = "");

void send_http_request(IStream& stream, const ParsedURL& parsed_url, const ClientConfig& config, const std::string current_cookie = "");

std::optional<std::string> server_response_to_text(IStream& stream, const int verbosity);

std::optional<HttpResponseData> process_http_response(const std::string& headers_text);