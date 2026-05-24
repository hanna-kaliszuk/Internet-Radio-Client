#include "tls_stream.h"

#include <unistd.h>
#include <cerrno>
#include <iostream>
#include <stdexcept>

#include <openssl/err.h>

/**
 * @brief Constructs a new TLS Stream over an existing socket descriptor.
 * @param fd The raw TCP socket descriptor.
 * @param hostname The target server hostname (for SNI).
 */
TlsStream::TlsStream(int fd, const std::string &hostname) : socket_fd(fd), ctx(nullptr), ssl(nullptr) {
    // tsl method for client
    const SSL_METHOD *method = TLS_client_method();
    ctx = SSL_CTX_new(method);

    if (!ctx) {
        close();
        throw std::runtime_error("unable to create SSL context");
    }

    ssl = SSL_new(ctx);
    if (!ssl) {
        close();
        throw std::runtime_error("unable to create SSL object");
    }

    // SNI
    SSL_set_tlsext_host_name(ssl, hostname.c_str());

    SSL_set_fd(ssl, socket_fd);

    // tls handshake
    if (SSL_connect(ssl) <= 0) {
        close();
        throw std::runtime_error("TLS Handshake failed for host: " + hostname);
    }
}

/**
 * @brief Destructor ensures TLS teardown and socket closure.
 */
TlsStream::~TlsStream() {
    close();
}

/**
 * @brief Reads encrypted payload from the socket via OpenSSL.
 */
ssize_t TlsStream::read(void *buffer, size_t count) {
    if (!ssl) {
        errno = EBADF;
        return -1;
    }

    ERR_clear_error();

    int const n = SSL_read(ssl, buffer, static_cast<int>(count));

    if (n > 0) {
        return static_cast<ssize_t>(n);
    }

    int err = SSL_get_error(ssl, n);

    switch (err) {
        case SSL_ERROR_ZERO_RETURN:
            return 0;

        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            errno = EAGAIN;
            return -1;

        case SSL_ERROR_SYSCALL:
            if (errno == 0) {
                errno = ECONNRESET;
            }
            return -1;

        case SSL_ERROR_SSL:
        default:
            errno = EIO;
            return -1;
    }
}

/**
 * @brief Writes encrypted payload to the socket via OpenSSL.
 */
ssize_t TlsStream::write(const void *buffer, size_t count) {
    if (!ssl) {
        errno = EBADF;
        return -1;
    }

    ERR_clear_error();

    int n = SSL_write(ssl, buffer, static_cast<int>(count));

    if (n > 0) {
        return static_cast<ssize_t>(n);
    }

    int err = SSL_get_error(ssl, n);

    switch (err) {
        case SSL_ERROR_ZERO_RETURN:
            return 0;

        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            errno = EAGAIN;
            return -1;

        case SSL_ERROR_SYSCALL:
            if (errno == 0) {
                errno = ECONNRESET;
            }
            return -1;

        case SSL_ERROR_SSL:
        default:
            errno = EIO;
            return -1;
    }
}

/**
 * @brief Gracefully terminates the TLS session and closes the TCP socket.
 */
void TlsStream::close() {
    if (ssl) {
        int result = SSL_shutdown(ssl);

        if (result == 0) {
            SSL_shutdown(ssl);
        }

        SSL_free(ssl);
        ssl = nullptr;
    }

    if (ctx) {
        SSL_CTX_free(ctx);
        ctx = nullptr;
    }

    if (socket_fd != -1) {
        ::close(socket_fd);
        socket_fd = -1;
    }
}

/**
 * @brief Retrieves the raw file descriptor for multiplexing (e.g., poll/select).
 */
int TlsStream::get_fd() const {
    return socket_fd;
}
