#pragma once

#include <cstddef>
#include <sys/types.h>
#include <unistd.h>

class IStream {
public:
    // a virtual deconstructor
    virtual ~IStream() = default;

    virtual ssize_t read(void* buf, size_t count) = 0;
    virtual ssize_t write(const void* buf, size_t count) = 0;
    virtual void close() = 0;
};

class TcpStream : public IStream {
private:
    int socket_fd;

public:
    explicit TcpStream(int fd) : socket_fd(fd) {}

    ~TcpStream() override {
        close();
    }

    ssize_t read(void* buf, size_t count) override {
        if (socket_fd == -1) return -1;
        return ::read(socket_fd, buf, count);
    }

    ssize_t write(const void* buf, size_t count) override {
        if (socket_fd == -1) return -1;
        return ::write(socket_fd, buf, count);
    }

    void close() override {
        if (socket_fd != -1) {
            ::close(socket_fd);
            socket_fd = -1;
        }
    }
};