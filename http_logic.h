#pragma once

#include <string>
#include <vector>

#include "client_config.h"
#include "network_logic.h"

/**
 * @brief Stores parsed data from an HTTP response.
 */
struct HttpResponseData {
    int status_code = 0;
    std::string new_location;
    std::vector<std::string> cookies;
    size_t icy_metaint = 0;
    bool critical_error = false;
};

/**
 * @brief Result of reading HTTP headers from the socket.
 */
struct HeaderReadResult {
    StreamResult result = StreamResult::OK;
    std::string text;
};

/**
 * @brief Constructs an HTTP GET request string.
 */
std::string build_http_request(const ParsedURL &parsed_url, const ClientConfig &config,
                               const std::string &current_cookie = "");

/**
 * @brief Sends the constructed HTTP request over the provided stream.
 */
void send_http_request(IStream &stream, const ParsedURL &parsed_url, const ClientConfig &config,
                       const std::string current_cookie = "");


/**
 * @brief Reads from the stream byte-by-byte until the \r\n\r\n delimiter is found.
 */
HeaderReadResult server_response_to_text(IStream &stream, const int verbosity);

/**
 * @brief Parses the raw HTTP response headers into an HttpResponseData struct.
 */
std::optional<HttpResponseData> process_http_response(const std::string &headers_text, const int verbosity);
