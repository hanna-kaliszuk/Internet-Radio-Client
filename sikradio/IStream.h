#pragma once

#include <sys/types.h>

/**
 * @brief Interface for generic data streams (TCP or TLS).
 */
class IStream {
public:
    virtual ~IStream() = default;

    /**
     * @brief Reads up to 'count' bytes into 'buf'.
     * @return Number of bytes read, 0 on EOF, or -1 on error.
     */
    virtual ssize_t read(void *buf, size_t count) = 0;

    /**
     * @brief Writes up to 'count' bytes from 'buf'.
     * @return Number of bytes written, or -1 on error.
     */
    virtual ssize_t write(const void *buf, size_t count) = 0;

    /**
     * @brief Closes the underlying socket/connection.
     */
    virtual void close() = 0;

    /**
     * @brief Retrieves the raw file descriptor.
     * @return The socket file descriptor.
     */
    virtual int get_fd() const = 0;
};

/**
 * @brief Represents the current state of the ICY demultiplexer.
 */
enum class StreamState {
    AUDIO,
    MULTIPLIER, METADATA
};

/**
 * @brief Represents the outcome of a streaming operation.
 */
enum class StreamResult {
    OK,
    TIMEOUT,
    CLOSED_BY_SERVER,
    STOPPED_BY_CLIENT
};
