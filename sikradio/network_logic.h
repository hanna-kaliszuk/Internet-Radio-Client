#pragma once

#include "client_config.h"
#include "url_parser.h"

/**
 * @brief Coordinates the process of connecting to the server:
 * from the DNS query, through establishing the TCP connection, to configuring the socket.
 */
int connect_to_server(const ParsedURL& parsed_url, const ClientConfig& config);