#pragma once

#include "IStream.h"
#include <string>
#include <sys/types.h>
#include <openssl/ssl.h>

class TlsStream : public IStream {
private:
    int socket_fd;
    SSL_CTX *ctx;
    SSL *ssl;

public:
    TlsStream(int fd, const std::string &hostname);

    ~TlsStream() override;

    ssize_t read(void *buffer, size_t count) override;

    ssize_t write(const void *buffer, size_t count) override;

    void close() override;

    int get_fd() const override;
};
