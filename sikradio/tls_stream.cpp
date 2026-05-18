#include "tls_stream.h"
#include <openssl/err.h>
#include <stdexcept>
#include <unistd.h>
#include <iostream>

TlsStream::TlsStream(int fd, const std::string& hostname) : socket_fd(fd), ctx(nullptr), ssl(nullptr) {
    // tsl method for client
    const SSL_METHOD* method = TLS_client_method();
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
        ERR_print_errors_fp(stderr);
        close();
        throw std::runtime_error("TLS Handshake failed for host: " + hostname);
    }
}

TlsStream::~TlsStream() {
    close();
}

ssize_t TlsStream::read(void* buffer, size_t count) {
    if (!ssl) return -1;

    int const n = SSL_read(ssl, buffer, static_cast<int>(count));

    return static_cast<ssize_t>(n);
}

ssize_t TlsStream::write(const void* buffer, size_t count) {
    if (!ssl) return -1;

    int const n = SSL_write(ssl, buffer, static_cast<int>(count));

    return static_cast<ssize_t>(n);
}

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