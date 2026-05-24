#pragma once

#include "client_config.h"
#include "url_parser.h"

#include <memory>
#include "IStream.h"

/**
 * @brief Coordinates the process of connecting to the server:
 * from the DNS query, through establishing the TCP connection, to configuring the socket.
 */
std::unique_ptr<IStream> connect_to_server(const ParsedURL &parsed_url, const ClientConfig &config);
