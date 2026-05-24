#include "tcp_stream.h"

TcpStream::TcpStream(int fd) : socket_fd(fd) {}

TcpStream::~TcpStream() {
    close();
}

ssize_t TcpStream::read(void* buf, size_t count) {
    if (socket_fd == -1) return -1;
    return ::read(socket_fd, buf, count);
}

ssize_t TcpStream::write(const void* buf, size_t count) {
    if (socket_fd == -1) return -1;
    return ::write(socket_fd, buf, count);
}

void TcpStream::close() {
    if (socket_fd != -1) {
        ::close(socket_fd);
        socket_fd = -1;
    }
}

int TcpStream::get_fd() {
    return socket_fd;
}