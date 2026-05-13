#pragma once

#include <string>
#include "client_config.h"
#include "network_logic.h"

std::string build_http_request(const ParsedURL& parsed_url, const ClientConfig& config, const std::string& current_cookie = "");