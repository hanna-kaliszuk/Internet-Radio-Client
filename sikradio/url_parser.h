#pragma once

#include <optional>
#include <string>

enum class Protocol {
    HTTP,
    HTTPS
};

namespace ProtocolOffsets {
    constexpr size_t HTTP = 7;
    constexpr size_t HTTPS = 8;
};

namespace DefaultPort {
    constexpr const char *HTTP = "80";
    constexpr const char *HTTPS = "443";
}

struct ParsedURL {
    Protocol protocol;
    std::string hostname;
    std::string port_str;
    std::string path;
};

// Divides URL into parts: protocol, host
std::optional<ParsedURL> parseUrl(const std::string& url);

