#include "network_logic.h"

#include <sys/socket.h>
#include <netdb.h>
#include <iostream>
#include <memory>

using AddrInfoPtr = std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)>;

/**
 * @brief Resolves the hostname into a list of available IP addresses. If provided, takes into account configuration
 * preferences (IPv4 / IPv6) and returns a smart pointer managing memory allocated by getaddrinfo.
 */
static AddrInfoPtr resolve_hostname(const ParsedURL& parsed_url, const ClientConfig& config) {
    struct addrinfo hints = {};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // get IP version or let getaddrinfo decide
    if (config.force_ipv4) {
        hints.ai_family = AF_INET;
    } else if (config.force_ipv6) {
        hints.ai_family = AF_INET6;
    } else {
        hints.ai_family = AF_UNSPEC;
    }

    struct addrinfo *raw_result = nullptr;
    int errcode = getaddrinfo(parsed_url.hostname.c_str(), parsed_url.port_str.c_str(), &hints, &raw_result);

    if (errcode != 0) {
        throw std::runtime_error(std::string("getaddrinfo: ") + gai_strerror(errcode));
    }

    AddrInfoPtr result(raw_result, &freeaddrinfo);
    return result;
}

/**
 * @brief Iterates through the list of IP addresses and attempts to establish a TCP connection.
 * Returns the file descriptor of the first successfully connected socket,  or throws an exception if all connection
 * attempts fail.
 */
static int connect_to_the_first_working_address(const struct addrinfo* addresses) {
    int socket_fd = -1;
    for (auto rp = addresses; rp != nullptr; rp = rp->ai_next) {
        socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (socket_fd == -1) {
            continue;
        }

        if (connect(socket_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(socket_fd);
        socket_fd = -1;
    }

    // couldn't establish a connection
    if (socket_fd == -1) {
        throw std::runtime_error(std::string("couldn't establish a connection."));
    }

    return socket_fd;
}

/**
 * @brief Configures the maximum wait time (timeout) for receiving data from the socket.
 * Converts milliseconds into a timeval structure and applies it using the SO_RCVTIMEO flag.
 */
static void configure_socket_timeout(const int socket_fd, const uint32_t timeout) {
    struct timeval tv = {};
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv)) == -1) {
        close(socket_fd);
        throw std::runtime_error(std::string("failed to set socket receive timeout"));
    }
}

std::unique_ptr<IStream> connect_to_server(const ParsedURL& parsed_url, const ClientConfig& config) {
    AddrInfoPtr const resolved_address = resolve_hostname(parsed_url, config);

    int const socket_fd = connect_to_the_first_working_address(resolved_address.get());

    configure_socket_timeout(socket_fd, config.timeout);

    return std::make_unique<TcpStream>(socket_fd);
}
