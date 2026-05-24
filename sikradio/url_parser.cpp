#include "url_parser.h"

static bool is_valid_port(const std::string &port) {
    if (port.empty()) {
        return false;
    }

    for (char c: port) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    try {
        size_t pos = 0;
        int value = std::stoi(port, &pos);

        return pos == port.size() && value >= 1 && value <= 65535;
    } catch (...) {
        return false;
    }
}

std::optional<ParsedURL> parseUrl(const std::string &url) {
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
    size_t const slash_position = url.find('/', offset);
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

    size_t const hash_position = result.path.find('#');
    if (hash_position != std::string::npos) {
        result.path = result.path.substr(0, hash_position);
    }

    // check if there is a unusual port specified (number after the last ':')
    size_t const colon_position = host_buffer.rfind(':');
    size_t const bracket_position = host_buffer.rfind(']');

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
        result.hostname = result.hostname.substr(1, result.hostname.size() - 2);
    }

    // validate if there is even a host
    if (result.hostname.empty()) {
        return std::nullopt;
    }

    return result;
}
