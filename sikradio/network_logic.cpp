#include "network_logic.h"
#include "tcp_stream.h"
#include "tls_stream.h"
#include "IStream.h"

#include <sys/socket.h>
#include <netdb.h>
#include <iostream>
#include <memory>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <arpa/inet.h> 

using AddrInfoPtr = std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)>;

static std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&now_time);

    std::ostringstream oss;
    // format: YYYY.MM.DD HH.MM.SS
    oss << std::put_time(local_time, "%Y.%m.%d %H.%M.%S");
    return oss.str();
}

static void log_connection_attempt(const struct addrinfo* rp) {
    char ip_str[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;

    if (rp->ai_family == AF_INET) {
        auto* ipv4 = reinterpret_cast<struct sockaddr_in*>(rp->ai_addr);
        inet_ntop(AF_INET, &(ipv4->sin_addr), ip_str, sizeof(ip_str));
        port = ntohs(ipv4->sin_port);
        
        // format ipv4
        std::cerr << "connecting to server " << ip_str << ":" << port << "\n";
        
    } else if (rp->ai_family == AF_INET6) {
        auto* ipv6 = reinterpret_cast<struct sockaddr_in6*>(rp->ai_addr);
        inet_ntop(AF_INET6, &(ipv6->sin6_addr), ip_str, sizeof(ip_str));
        port = ntohs(ipv6->sin6_port);
        
        // format ipv6
        std::cerr << "connecting to server [" << ip_str << "]:" << port << "\n";
    }
}

/**
 * @brief Resolves the hostname into a list of available IP addresses. If provided, takes into account configuration
 * preferences (IPv4 / IPv6) and returns a smart pointer managing memory allocated by getaddrinfo.
 */
static AddrInfoPtr resolve_hostname(const ParsedURL& parsed_url, const ClientConfig& config) {
    std::cerr << get_current_timestamp() << "\n";
    std::cerr << "resolving name " << parsed_url.hostname << std::endl;

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
        log_connection_attempt(addresses);

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

    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv)) == -1) {
        close(socket_fd);
        throw std::runtime_error(std::string("failed to set socket receive timeout"));
    }
}

std::unique_ptr<IStream> connect_to_server(const ParsedURL& parsed_url, const ClientConfig& config) {
    AddrInfoPtr const resolved_address = resolve_hostname(parsed_url, config);

    int const socket_fd = connect_to_the_first_working_address(resolved_address.get());

    configure_socket_timeout(socket_fd, config.timeout);

    if (parsed_url.protocol == Protocol::HTTPS) {
        return std::make_unique<TlsStream>(socket_fd, parsed_url.hostname);
    } else {
        return std::make_unique<TcpStream>(socket_fd);
    }
}
