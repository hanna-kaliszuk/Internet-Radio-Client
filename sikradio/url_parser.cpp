#include "url_parser.h"

namespace {
    constexpr int MIN_PORT = 1;
    constexpr int MAX_PORT = 65535;
    constexpr size_t IPV6_BRACKET_OFFSET = 1;
    constexpr size_t IPV6_BRACKETS_LENGTH = 2; // "[" and "]"
}

/**
 * @brief Verifies if the provided port string is a valid numeric port (1-65535).
 * @param port The port candidate string.
 * @return True if valid, false otherwise.
 */
static bool is_valid_port(const std::string &port) {
    if (port.empty()) {
        return false;
    }

    for (const char c: port) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    try {
        size_t pos = 0;
        const int value = std::stoi(port, &pos);

        return pos == port.size() && value >= MIN_PORT && value <= MAX_PORT;
    } catch (...) {
        return false;
    }
}

/**
 * @brief Parses a full URL string into its protocol, host, port, and path components.
 * @param url The raw URL string provided by the user.
 * @return std::optional containing ParsedURL struct on success, nullopt on format failure.
 */
std::optional<ParsedURL> parse_url(const std::string &url) {
    ParsedURL result;

    size_t offset = 0;

    // find out which protocol and set default ports
    if (url.starts_with("http://")) {
        result.protocol = Protocol::HTTP;
        result.port_str = DefaultPort::HTTP;
        offset = ProtocolOffsets::HTTP;
    } else if (url.starts_with("https://")) {
        result.protocol = Protocol::HTTPS;
        result.port_str = DefaultPort::HTTPS;
        offset = ProtocolOffsets::HTTPS;
    } else {
        result.protocol = Protocol::HTTP;
        result.port_str = DefaultPort::HTTP;
    }

    // find the path
    // default: '/'
    const size_t slash_position = url.find('/', offset);
    std::string host_buffer;

    if (slash_position != std::string::npos) {
        // path found
        result.path = url.substr(slash_position);
        host_buffer = url.substr(offset, slash_position - offset);
    } else {
        // path not found
        result.path = "/"; // empty path
        host_buffer = url.substr(offset);
    }

    const size_t hash_position = result.path.find('#');
    if (hash_position != std::string::npos) {
        result.path = result.path.substr(0, hash_position);
    }

    // check if there is a unusual port specified (number after the last ':')
    const size_t colon_position = host_buffer.rfind(':');
    const size_t bracket_position = host_buffer.rfind(']');

    if (colon_position != std::string::npos &&
        (bracket_position == std::string::npos || colon_position > bracket_position)) {
        result.port_str = host_buffer.substr(colon_position + 1); // +1 not to include ':'
        if (!is_valid_port(result.port_str)) {
            return std::nullopt;
        }

        result.hostname = host_buffer.substr(0, colon_position);
    } else {
        // no unusual port provided
        result.hostname = host_buffer;
    }

    // if it was raw IPv6 address, strip it off the brackets
    if (!result.hostname.empty() && result.hostname.front() == '[' && result.hostname.back() == ']') {
        result.hostname = result.hostname.substr(IPV6_BRACKET_OFFSET, result.hostname.size() - IPV6_BRACKETS_LENGTH);
    }

    // validate if there is even a host
    if (result.hostname.empty()) {
        return std::nullopt;
    }

    return result;
}
