#include "tcp_stream.h"

/**
 * @brief Constructs a new TcpStream wrapping the provided file descriptor.
 * @param fd The socket file descriptor.
 */
TcpStream::TcpStream(const int fd) : socket_fd(fd) {
}

/**
 * @brief Destructor ensuring the socket gets closed gracefully.
 */
TcpStream::~TcpStream() {
    close();
}

/**
 * @brief Reads data from the TCP socket.
 */
ssize_t TcpStream::read(void *buf, size_t count) {
    if (socket_fd == -1) return -1;
    return ::read(socket_fd, buf, count);
}

/**
 * @brief Writes data to the TCP socket.
 */
ssize_t TcpStream::write(const void *buf, size_t count) {
    if (socket_fd == -1) return -1;
    return ::write(socket_fd, buf, count);
}

/**
 * @brief Closes the connection and sets descriptor to -1.
 */
void TcpStream::close() {
    if (socket_fd != -1) {
        ::close(socket_fd);
        socket_fd = -1;
    }
}

/**
 * @brief Getter for the raw file descriptor.
 */
int TcpStream::get_fd() const {
    return socket_fd;
}
