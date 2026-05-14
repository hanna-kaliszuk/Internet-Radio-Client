#pragma once

#include "IStream.h"
#include <sys/types.h>
#include <unistd.h>

class TcpStream : public IStream {
private:
    int socket_fd;

public:
    TcpStream(int fd);

    ~TcpStream() override;

    ssize_t read(void* buffer, size_t count) override;

    ssize_t write(const void* buf, size_t count) override;

    void close() override;
};